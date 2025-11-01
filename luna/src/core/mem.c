// mem.c

#include "common.h"

#include "mem.h"

#define MEMORY_POOL_SIZE (1024 * 1024)

#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1))

typedef struct mem_block {
  size_t size;
  int free;
  struct mem_block* next;
} mem_block_t;

static unsigned char memory_pool[MEMORY_POOL_SIZE];

static mem_block_t* free_list = NULL;

void minit() {
  free_list = (mem_block_t*)memory_pool;
  free_list->size = MEMORY_POOL_SIZE - sizeof(mem_block_t);
  free_list->free = 1;
  free_list->next = NULL;
}

void split_block(mem_block_t* fitting_slot, size_t size) {
  mem_block_t* new_block = (void*)((unsigned char*)fitting_slot + sizeof(mem_block_t) + size);
  new_block->size = fitting_slot->size - size - sizeof(mem_block_t);
  new_block->free = 1;
  new_block->next = fitting_slot->next;
  fitting_slot->size = size;
  fitting_slot->free = 0;
  fitting_slot->next = new_block;
}

void merge_blocks() {
  mem_block_t* current = free_list;
  while (current && current->next) {
    if (current->free && current->next->free) {
      current->size += current->next->size + sizeof(mem_block_t);
      current->next = current->next->next;
    } else {
      current = current->next;
    }
  }
}

void *mmalloc(unsigned int size) {
  mem_block_t* current;
  void* result;

  size = ALIGN(size);

  if (!free_list) {
    minit();
  }

  current = free_list;

  while (current != NULL) {
    if (current->free && current->size >= size) {
      break;
    }
    current = current->next;
  }

  if (current == NULL) {
    return NULL;
  }

  if (current->size >= size + sizeof(mem_block_t) + ALIGNMENT) {
    split_block(current, size);
  } else {
    current->free = 0;
  }

  result = (void*)((unsigned char*)current + sizeof(mem_block_t));
  return result;
}

void* mmrealloc(void* ptr, size_t new_size) {
  new_size = ALIGN(new_size);

  if (!ptr) {
    return mmalloc(new_size);
  }

  mem_block_t* block = (mem_block_t*)((unsigned char*)ptr - sizeof(mem_block_t));
  if (block->size >= new_size) {
    return ptr;
  } else {
    void* new_ptr = mmalloc(new_size);
    if (new_ptr) {
      mcopy(new_ptr, ptr, block->size);
      mfree(ptr);
    }
    return new_ptr;
  }
}

void mfree(void *ptr) {
  if (NULL == ptr) {
    return;
  }

  mem_block_t* current = (mem_block_t*)((unsigned char*)ptr - sizeof(mem_block_t));
  current->free = 1;
  merge_blocks();
}

void* mcopy(void* dest, const void* src, size_t num) {
  unsigned char* d = (unsigned char*)dest;
  const unsigned char* s = (const unsigned char*)src;
  size_t i;

  for (i = 0;i < num; i++) {
    d[i] = s[i];
  }

  return dest;
}

void* mset(void* ptr, int value, size_t num) {
  unsigned char* p = (unsigned char*)ptr;
  size_t i;

  for (i=0; i < num; i++) {
    p[i] = (unsigned char)value;
  }

  return ptr;
}

void mreset() {
    minit();
}
