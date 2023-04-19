// SPDX-License-Identifier: BSD-3-Clause

#include "osmem.h"
#include "helpers.h"

int get_alligned_size(size_t size)
{
	if(size % ALIGNMENT == 0){
		return size + BLOCK_SIZE;
	}else {
		return (size / ALIGNMENT + 1) * (ALIGNMENT) + BLOCK_SIZE; // multiplu de 8
	}
}

void* merge_blocks(struct block_meta *start, struct block_meta *end)
{
	struct block_meta *aux = start;
	struct block_meta *curr = start;
	while(curr != end){
		start->size += curr->next->size; // adun memoria block-ului care este scos din lista

		aux = curr->next; // pastrez referinta catre urmatorul block
		start->next = curr->next->next; // refac legaturile a.i. primul block sa pointeze catre urmatorul block alocat
										// sau sfarsitul listei(in functie de caz)
		curr = aux;
	}
}



void* find_best_block(size_t final_size)
{
	struct block_meta *curr = block;
	struct block_meta *best = NULL;
	int min = INT_MAX;
	while(curr != NULL){
		if(curr->status == STATUS_FREE){
			if(curr->size >= final_size){
				if(curr->size - final_size < min){
					min = curr->size - final_size;
					best = curr;
				}
			}
			if(curr->size < final_size){
				struct block_meta *coalesce = curr;
				int size_sum = 0;
				// verific daca pot face merge cu unul sau mai multe block-uri adiancente libere
				while(curr != NULL && curr->status == STATUS_FREE){
					// verific daca sunt destule block-uri libere consecutive a.i. 
					// sa le pot combina in unul cu size destul de mare
					size_sum += curr->size;
					if(final_size <= size_sum || curr->next == NULL){
						// lipesc block-urile de la coalesce pana la curr.
						merge_blocks(coalesce, curr);
						return coalesce;
					}
					curr = curr->next;
				}
				curr = coalesce;
			}
		}
		curr = curr->next;
	}
	if(best != NULL)
	return best;
}

void *os_malloc(size_t size)
{
	if(size <= 0){
		return NULL;
	}
	int final_size = get_alligned_size(size);
	// prima alocare de memorie
	if(block == NULL){
		if(final_size < MMAP_THRESHOLD){
			block = sbrk(0);
			void *mem_request = sbrk(MMAP_THRESHOLD);
			DIE((void*)block != mem_request, "Srbk not working");
			// verific daca pot sa impart zona de memorie in doua block-uri
			if(MMAP_THRESHOLD - final_size >= BLOCK_SIZE + ALIGNMENT){
				struct block_meta *new_cell = (struct block_meta*)((void*)block + final_size);
				new_cell->size = MMAP_THRESHOLD - final_size;
				new_cell->status = STATUS_FREE;
				new_cell->next = NULL;

				block->size = final_size;
				block->next = new_cell;
				block->status = STATUS_ALLOC;
			} else {
				block->size = final_size;
				block->next = NULL;
				block->status = STATUS_ALLOC;
			}
			return block + 1;

		} else if (final_size >= MMAP_THRESHOLD){
			struct block_meta *new_cell = mmap(NULL, final_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			DIE((void*)new_cell == MAP_FAILED, "mmap not working");
			new_cell->size = final_size;
			new_cell->next = NULL;
			new_cell->status = STATUS_MAPPED;
			return new_cell + 1;
		}
	} else {
		struct block_meta *best = (struct block_meta*)find_best_block(final_size);
		if(best != NULL && best->size >= final_size){
			if(best->size - final_size < BLOCK_SIZE + 8){
				// block-ul nu poate fi impartit in doua, deci voi folosi toata
				// memoria disponibila din el.
				best->status = STATUS_ALLOC;
				return best + 1; // adresa de dupa structura block_meta
			} else {
				// block-ul are o capacitate destul de mare incat sa poata fi
				// stocat inca un block(la un viitor apel de malloc), asa ca
				// il impart in doua.
				struct block_meta *new_cell = (struct block_meta*)((char*)best + final_size);
				new_cell->size = best->size - final_size;
				new_cell->status = STATUS_FREE;
				new_cell->next = best->next;
				best->next = new_cell;
				best->status = STATUS_ALLOC;
				best->size = final_size;
				return best + 1;
			}
		} else { // nu am basit un bloc liber, deci extind zona de memorie
			struct block_meta *last_block = block;
			while(last_block->next != NULL){
				last_block = last_block->next;
			}
			// ultimul block este liber, deci il extind, fara sa creez unul nou
			if(last_block->status == STATUS_FREE){
				sbrk(final_size - last_block->size);
				last_block->size = final_size;
				last_block->status = STATUS_ALLOC;
				return last_block + 1;
			}
			// este nevoie sa creez un nou block
			if(size < MMAP_THRESHOLD){
				struct block_meta *new_cell = sbrk(final_size);
				new_cell->size = final_size;
				new_cell->next = NULL;
				new_cell->status = STATUS_ALLOC;
				last_block->next = new_cell;
				return new_cell + 1;
			} else if (final_size >= MMAP_THRESHOLD){

				struct block_meta *new_cell = mmap(NULL, final_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

				DIE((void*)new_cell == MAP_FAILED, "mmap not working");

				new_cell->size = final_size;
				new_cell->next = NULL;
				new_cell->status = STATUS_MAPPED;

				return new_cell + 1;
			}
		}
	}
	 return NULL;
}

void os_free(void *ptr)
{
	if(ptr == NULL){
		return NULL;
	}
	struct block_meta *b = (struct block_meta*)(ptr - BLOCK_SIZE);

	if(b->status == STATUS_MAPPED){
		munmap(ptr - BLOCK_SIZE, b->size);
	}else {
		b->status = STATUS_FREE;
	}
}

void *os_calloc(size_t nmemb, size_t size)
{
	if(size <= 0 || nmemb == 0){
		return NULL;
	}
	int final_size = get_alligned_size(size * nmemb);
	// prima alocare de memorie
	if(block == NULL){
		if(final_size < PAGE_SIZE){
			block = sbrk(0);
			void *mem_request = sbrk(MMAP_THRESHOLD);
			DIE((void*)block != mem_request, "Srbk not working");
			// verific daca pot sa impart zona de memorie in doua block-uri
			if(MMAP_THRESHOLD - final_size >= BLOCK_SIZE + ALIGNMENT){
				struct block_meta *new_cell = (struct block_meta*)((void*)block + final_size);
				new_cell->size = MMAP_THRESHOLD - final_size;
				new_cell->status = STATUS_FREE;
				new_cell->next = NULL;

				block->size = final_size;
				block->next = new_cell;
				block->status = STATUS_ALLOC;
			} else {
				block->size = final_size;
				block->next = NULL;
				block->status = STATUS_ALLOC;
			}
			return block + 1;
		} else if (final_size >= PAGE_SIZE){
			struct block_meta *new_cell = mmap(NULL, final_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

			DIE((void*)new_cell == MAP_FAILED, "mmap not working");
			new_cell->size = final_size;
			new_cell->next = NULL;
			new_cell->status = STATUS_MAPPED;
			return new_cell + 1;
		}
		memset(block + 1, 0, final_size);

	} else {
		struct block_meta *best = (struct block_meta*)find_best_block(final_size);
		if(best != NULL && best->size >= final_size){
			if(best->size - final_size < BLOCK_SIZE + 8){
				// block-ul nu poate fi impartit in doua, deci voi folosi toata
				// memoria disponibila din el.
				best->status = STATUS_ALLOC;
				memset(best + 1, 0, best->size - BLOCK_SIZE); // daca block-ul era alocat cu malloc,
															  // trebuie setata memoria corespunzator
				return best + 1; // adresa de dupa structura block_meta
			} else {
				// block-ul are o capacitate destul de mare incat sa poata fi
				// stocat inca un block(la un viitor apel de calloc), asa ca
				// il impart in doua.
				struct block_meta *new_cell = (struct block_meta*)((char*)best + final_size); // block-ul care ramane gol
				new_cell->size = best->size - final_size;
				new_cell->status = STATUS_FREE;
				new_cell->next = best->next;

				memset(new_cell + 1, 0, best->size - final_size - BLOCK_SIZE);

				best->next = new_cell;
				best->status = STATUS_ALLOC;
				best->size = final_size;

				memset(best + 1, 0, final_size - BLOCK_SIZE);
				return best + 1;
			}
		} else { // nu am basit un block liber, deci extind zona de memorie
			struct block_meta *last_block = block;
			while(last_block->next != NULL){
				last_block = last_block->next;
			}
			// ultimul block este liber, deci il extind, fara sa creez unul nou
			if(last_block->status == STATUS_FREE){

				void* mem_to_be_set = sbrk(final_size - last_block->size);

				DIE(mem_to_be_set == (void*)-1, "Calloc not working\n");
				last_block->size = final_size;
				last_block->status = STATUS_ALLOC;

				memset(last_block + 1, 0, final_size - BLOCK_SIZE);
				return last_block + 1;
			}
			// este nevoie sa creez un block nou
			if(final_size < PAGE_SIZE){
				struct block_meta *new_cell = sbrk(final_size);
				new_cell->size = final_size;
				new_cell->next = NULL;
				new_cell->status = STATUS_ALLOC;

				memset(new_cell + 1, 0, final_size - BLOCK_SIZE);

				last_block->next = new_cell;
				return new_cell + 1;
			} else if (final_size >= PAGE_SIZE){

				struct block_meta *new_cell = mmap(NULL, final_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				DIE((void*)new_cell == MAP_FAILED, "mmap not working");

				new_cell->size = final_size;
				new_cell->next = NULL;
				new_cell->status = STATUS_MAPPED;

				memset(new_cell + 1, 0, final_size - BLOCK_SIZE);
				return new_cell + 1;
			}
		}
	}
	 return NULL;
}

void os_truncate(struct block_meta* curr, size_t size)
{	
	struct block_meta *new_cell = (struct block_meta*)((char*)curr + size);
	new_cell->size = curr->size - size;
	new_cell->status = STATUS_FREE;
	new_cell-> next = curr->next;

	curr->size = size;
	curr->next = new_cell;
}

int extend(struct block_meta *prev, size_t size)
{
	struct block_meta *second_last = prev;
	if(prev->size == size){
		return 1;
	}
	while(prev->next != NULL){

		if(prev->next->status == STATUS_FREE){
			merge_blocks(prev, prev->next);
			if(prev->size >= size){
				return 1;
			}
		} else {
			break;
		}
		second_last = prev;
		prev = prev->next;
	}
	if(prev != NULL){
		if(prev->status == STATUS_FREE){
			merge_blocks(second_last, prev);
			if(second_last->size >= size){
				return 1;
			}
		}
	}
	return 0;
}

void* find_best_block_realloc(size_t final_size)
{
	struct block_meta *curr = block;
	struct block_meta *best = NULL;
	int min = INT_MAX;
	while(curr != NULL){
		if(curr->status == STATUS_FREE){
			if(curr->size >= final_size){
				if(curr->size - final_size < min){
					min = curr->size - final_size;
					best = curr;
				}
			}
				if(curr->size < final_size){
				struct block_meta *coalesce = curr;
				int size_sum = 0;
				// verific daca pot face merge cu unul sau mai multe block-uri adiancente libere
				while(curr != NULL && curr->status == STATUS_FREE){
					// verific daca sunt destule block-uri libere consecutive a.i.
					// sa le pot combina in unul cu size destul de mare
					size_sum += curr->size;
					if(final_size <= size_sum){
						// lipesc block-urile de la coalesce pana la curr.
						merge_blocks(coalesce, curr);
						return coalesce;
					}
					curr = curr->next;
				}
				curr = coalesce;
			}
		}
		curr = curr->next;
	}
	if(best != NULL)
	return best;
}


void *os_realloc(void *ptr, size_t size)
{
	if(ptr == NULL){
		return os_malloc(size);
	}
	
	if(size == 0){
		os_free(ptr);
		return NULL;
	}
	
	struct block_meta *aux = block;
	while(aux != NULL){
		aux = aux->next;
	}
	struct block_meta *prev = (struct block_meta*)(ptr - BLOCK_SIZE);
	if(block == NULL && ptr != NULL){
		void* ret = os_malloc(size);
		munmap(prev, prev->size);
		return ret;
	}
	int final_size = get_alligned_size(size);

	if(final_size >= prev->size){
		if(extend(prev, final_size) == 1){ // initial incerc sa extind block-ul curent
			return prev + 1;
		}else { // trebuie sa maresc zona de memorie

			struct block_meta *best = (struct block_meta*)find_best_block_realloc(final_size);
			if(best != NULL){
				if(best->size - final_size < BLOCK_SIZE + 8){
					// block-ul nu poate fi impartit in doua, deci voi folosi toata
					// memoria disponibila din el.
					best->status = STATUS_ALLOC;
					return best + 1; // adresa de dupa structura block_meta
				} else {
					// block-ul are o capacitate destul de mare incat sa poata fi
					// stocat inca un block(la un viitor apel de malloc), asa ca
					// il impart in doua.
					struct block_meta *new_cell = (struct block_meta*)((char*)best + final_size);
					new_cell->size = best->size - final_size;
					new_cell->status = STATUS_FREE;
					new_cell->next = best->next;
					memcpy(best + 1, prev + 1, prev->size);
					best->next = new_cell;
					best->status = STATUS_ALLOC;
					best->size = final_size;

					prev->status = STATUS_FREE;
					return best + 1;
				}
			}
			struct block_meta *last_block = block;
			while(last_block->next != NULL){
				last_block = last_block->next;
			}
			if(last_block->status == STATUS_FREE || last_block == prev){ // extind ultimul block daca e liber
				// sau daca block-ul pe care vreau sa il extind se intampla sa fie ultimul din lista
				sbrk(final_size - last_block->size);
				last_block->size = final_size;
				last_block->status = STATUS_ALLOC;
				return last_block + 1;
			}
			
			// este nevoie sa creez un block nou
			if(size < MMAP_THRESHOLD){
				
				struct block_meta *new_cell = sbrk(final_size);
				// copiez datele in zona noua de memorie
				memcpy(new_cell + 1, prev + 1, prev->size);
				
				new_cell->size = final_size + prev->size;
				new_cell->next = NULL;
				new_cell->status = STATUS_ALLOC;
				last_block->next = new_cell;
				prev->status = STATUS_FREE;
				return new_cell + 1;

			} else if (final_size >= MMAP_THRESHOLD){

				struct block_meta *new_cell = mmap(NULL, final_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

				DIE((void*)new_cell == MAP_FAILED, "mmap not working");
				
				// copiez datele in zona noua de memorie
				memcpy(new_cell + 1, prev + 1, prev->size);

				new_cell->size = final_size;
				new_cell->next = NULL;
				new_cell->status = STATUS_MAPPED;
				prev->status = STATUS_FREE;
				return new_cell + 1;
			}
		}
	} else if(final_size < prev->size){
		if(prev->size - final_size >= BLOCK_SIZE + ALIGNMENT){
			os_truncate(prev, final_size);
		}
		return prev + 1;
	}
	return NULL;
}
