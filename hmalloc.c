
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdint.h>

#include "hmalloc.h"

static size_t *DEBUG_TRACKER;

/*
  typedef struct hm_stats {
  long pages_mapped;
  long pages_unmapped;
  long chunks_allocated;
  long chunks_freed;
  long free_length;
  } hm_stats;
*/

typedef struct free_list_node
{
    size_t size;
    struct free_list_node *next;
} free_list_node;

const size_t PAGE_SIZE = 4096;
static hm_stats stats; // This initializes the stats to 0.
static free_list_node *head;
static free_list_node *DEBUG_NODE;
static size_t DEBUG_NODE_VALUE;

void add_to_fl(void *memaddr, size_t size)
{
    free_list_node *workingNode = head;
    while (workingNode->next != NULL)
    {
        // if the new node is less than the next node
        // then the new node should be this node's next node
        if (memaddr < (void *)workingNode->next)
        {
            free_list_node *newNode = memaddr;
            newNode->next = workingNode->next;
            newNode->size = size;
            workingNode->next = newNode;
            return;
        }
        workingNode = workingNode->next;
    }
    //node belongs on the end, it is the biggest
    free_list_node *newNode = memaddr;
    newNode->next = workingNode->next;
    newNode->size = size;
    workingNode->next = newNode;
}

void free_list_coalsece()
{
    free_list_node *workingNode = head->next;
    size_t DEBUG_INDEX = 0;

    // while we are not at the end of the list
    while (workingNode != NULL)
    {
        DEBUG_INDEX++;
        if (workingNode->next != NULL)
        {

            void *myAddress = workingNode;
            void *nextAddress = workingNode->next;
            //check to see if next points to the address right at the end of this node
            void *addr = myAddress + (sizeof(free_list_node) + workingNode->size);
            if (addr == nextAddress)
            {
                // gobble that dude up
                workingNode->size += workingNode->next->size + sizeof(free_list_node);
                workingNode->next = workingNode->next->next;
                continue;
            }
        }
        workingNode = workingNode->next;
    }
}

long free_list_length()
{
    long retval = 0;
    // the head is a sentinel, it doesn't count
    free_list_node *next = head->next;
    while (next != NULL)
    {
        retval++;
        next = next->next;
    }
    return retval;
}

hm_stats *
hgetstats()
{
    stats.free_length = free_list_length();
    return &stats;
}

void hprintstats()
{
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== husky malloc stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

static size_t
div_up(size_t xx, size_t yy)
{
    // This is useful to calculate # of pages
    // for large allocations.
    size_t zz = xx / yy;

    if (zz * yy == xx)
    {
        return zz;
    }
    else
    {
        return zz + 1;
    }
}

/**
 * @brief allocate a small chunk of memory, less than 4096 bytes
 * @param size the number of bytes to allocate
 * @return a void pointer to the freshly allocated memory
 */
void *hmallocHelperSmall(size_t size)
{
    // if(stats.chunks_allocated % 10 == 0)
    // {
    //     printf("checkpoint\n");
    // }
    // use this pointer to keep track of the current node
    free_list_node *currentNode = head;

    // if we don't allocate enough space for a free node eventually
    // then we will have some problems
    // make sure size is at least the size of a free node
    if (size < sizeof(free_list_node))
    {
        size = sizeof(free_list_node);
    }

    // keep looking for free nodes
    // IMPORTANT
    // to keep from messing up the linked list, we always work
    // with a node's -> next element, not the node itself
    // otherwise a node may move and break its previous element's reference
    // to itself

    // we need space for the alloc and the overhead
    // iterate over the whole list
    while (currentNode != NULL)
    {
        // end of the list and we still haven't found enough space?
        // make some more space
        if (currentNode->next == NULL)
        {
            // currentNode->next = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            // currentNode->next->size = PAGE_SIZE - sizeof(free_list_node);
            // currentNode->next->next = NULL;
            void * newPage = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
            size_t newFreeSize = PAGE_SIZE - sizeof(free_list_node);
            add_to_fl(newPage, newFreeSize);
            stats.pages_mapped++;
            // to ensure organization, we loop again
            currentNode = head;
            continue;
        }
        // found enough space? great, lets go
        if (currentNode->next->size >= size)
        {
            break;
        }
        // still more list elements? keep them coming
        currentNode = currentNode->next;
    }

    free_list_node *nextNode = currentNode->next;

    

    // once we get here, we know that either we found an existing block with enough space
    // or we mmaped a new block with enough space
    void *newMem = nextNode;

    // evacuate the free node to a new safe space
    size_t newSize = nextNode->size - size;
    // if the block will have < 16 bytes left after this, it will become useless garbage
    // just allocate it in its entirety
    free_list_node *newNext = nextNode->next;
    nextNode = newMem + size;
    if(newSize < sizeof(free_list_node))
    {
        size += newSize;
        newSize = 0;
    }

    

    nextNode->size = newSize;
    nextNode->next = newNext;

    // put the size value into the block
    size_t *memSize = (size_t *)newMem;

    *memSize = size;

    // if (memSize == 0x7ffff7ff5010)
    // {
    //     DEBUG_TRACKER = memSize;
    // }
    // if (nextNode == 0x7ffff79d0fdf)
    // {
    //     DEBUG_NODE = nextNode;
    //     printf("don't optimize me\n");
    // }
    // if (DEBUG_NODE != NULL)
    // {
    //     DEBUG_NODE_VALUE = DEBUG_NODE->size;
    // }
    // iterate the pointer so the user won't see the size
    newMem += sizeof(size_t);

    //correct the pointer
    currentNode->next = nextNode;

    

    return newMem;
}

/**
 * @brief allocate a big chunk of memory, more than 4096 bytes
 * @param size the number of bytes to allocate
 * @return a void pointer to the freshly allocated memory
 */
void *hmallocHelperBig(size_t size)
{
    // include the overhead size
    size_t pagesNeeded = div_up(size + sizeof(size_t), PAGE_SIZE);

    void *freshPages = mmap(NULL, pagesNeeded * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    *((size_t *)freshPages) = size;

    freshPages += sizeof(size_t);

    stats.pages_mapped += pagesNeeded;
    return freshPages;
}

void *
hmalloc(size_t size)
{
    
    static int firstTime = 1;
    if (firstTime)
    {
        static free_list_node dummy;
        // head = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        // // keep track of that now
        // stats.pages_mapped++;
        // initialize the head
        head = &dummy;
        head->size = 0;
        head->next = NULL;

        firstTime = 0;
    }

    stats.chunks_allocated += 1;
    size += sizeof(size_t);

    // TODO: Actually allocate memory with mmap and a free list.
    // allocs requring under 1 page are small
    if (size < PAGE_SIZE - sizeof(free_list_node))
    {
        return hmallocHelperSmall(size);
    }
    // otherwise they are big
    else
    {
        return hmallocHelperBig(size);
    }
}

void hfree(void *item)
{
    //printf("%ld\n", free_list_length());
    if (DEBUG_NODE != NULL)
    {
        DEBUG_NODE_VALUE = DEBUG_NODE->size;
    }
    stats.chunks_freed += 1;

    void *trueStart = item - (sizeof(size_t));

    size_t *newSizePtr = (size_t *)trueStart;
    size_t newSize = *newSizePtr;

    // if we used less than one page
    if (newSize < PAGE_SIZE - sizeof(size_t))
    {
        //free_list_node *newHead = trueStart;

        size_t nodeSize = newSize - sizeof(free_list_node);
        // newHead->size = nodeSize;
        // newHead->next = head->next;
        // head->next = newHead;
        add_to_fl(trueStart, nodeSize);

        if (newSize < 8)
        {
            // shit's fucked
            exit(0);
        }
    }
    else
    {
        size_t pagesNeeded = div_up(newSize + sizeof(size_t), PAGE_SIZE);

        munmap(item, pagesNeeded * PAGE_SIZE);

        stats.pages_unmapped += pagesNeeded;
    }

    free_list_coalsece();
    // TODO: Actually free the item.
}