#include "memory/zt_memory.h"
#include "containers/zt_list.h"
#include "synchronization/zt_atomic.h"

typedef struct ZTNode {
	struct ZTNode* next;
	void* value;

} ZTNode,*PZTNode;

typedef struct ZTList {
	PZTNode				head;
	PZTNode				tail;
	unsigned long long  size;
	volatile long		count;

} ZTList, * PZList;

bool zt_list_init(PZTList* list, size_t size) {

	if (list == NULL || *list != NULL || size == 0)
		return false;

	bool init_state   = false;
	PZTList temp_list = NULL;
	if (!zt_allocate(&temp_list, sizeof(ZTList)))
		goto cleanup;

	zt_memset(temp_list, 0, sizeof(ZTList));

	temp_list->head = NULL;
	temp_list->tail = NULL;
	temp_list->size = size;
	temp_list->count = 0;

	*list = temp_list;
	
	init_state = true;

cleanup:
	return init_state;

}

bool zt_list_deinit(PZTList* list) {

	if (list == NULL || *list == NULL)
		return false;

	PZTNode node = (*list)->head;
	while (node != NULL) {

		PZTNode next = node->next;
		zt_deallocate(&node->value);
		zt_deallocate(&node);

		node = next;
	}

	return zt_deallocate(list);

}

bool zt_list_push_back(PZTList list, void* entry) {

	if (list == NULL || entry == NULL)
		return false;

	bool push_state	   = false;
	PZTNode temp_entry = NULL;

	if (!zt_allocate(&temp_entry, sizeof(ZTNode)))
		goto cleanup;

	if (!zt_allocate(&temp_entry->value, list->size))
		goto cleanup;

	temp_entry->next  = NULL;
	zt_memset(temp_entry->value, 0, list->size);
	zt_memcpy(temp_entry->value, entry, list->size);
	if (list->tail)
		list->tail->next = temp_entry;
	else
		list->head = temp_entry;

	list->tail = temp_entry;
	
	zt_atomic_increment(&list->count);
	push_state = true;

cleanup:
	if (!push_state && temp_entry)
		zt_deallocate(&temp_entry);

	return push_state;

}

bool zt_list_push_front(PZTList list, void* entry) {
	if (list == NULL || entry == NULL)
		return false;

	bool push_state = false;
	PZTNode temp_entry = NULL;

	if (!zt_allocate(&temp_entry, sizeof(ZTNode)))
		goto cleanup;

	if (!zt_allocate(&temp_entry->value, list->size))
		goto cleanup;
	temp_entry->next = NULL;
	zt_memset(temp_entry->value, 0, list->size);

	zt_memcpy(temp_entry->value, entry, list->size);

	temp_entry->next = list->head;
	list->head = temp_entry;

	if (list->tail == NULL)
		list->tail = temp_entry;

	zt_atomic_increment(&list->count);
	push_state = true;

cleanup:
	if (!push_state && temp_entry)
		zt_deallocate(&temp_entry);

	return push_state;
}

bool zt_list_pop_back(PZTList list, void* entry) {
	if (list == NULL || entry == NULL)
		return false;

	bool pop_state	       = false;
	PZTNode current_entry  = NULL;
	PZTNode previous_entry = NULL;

	if (!list->tail)
		goto cleanup;

	current_entry = list->tail;
	zt_memcpy(entry, current_entry->value, list->size);

	if (list->head == list->tail) {
		list->head = NULL;
		list->tail = NULL;
	}
	else {
		previous_entry = list->head;
		while (previous_entry->next != list->tail)
			previous_entry = previous_entry->next;

		previous_entry->next = NULL;
		list->tail = previous_entry;
	}

	zt_deallocate(&current_entry->value);
	zt_deallocate(&current_entry);
	
	zt_atomic_decrement(&list->count);
	
	pop_state = true;

cleanup:

	return pop_state;
}

bool zt_list_pop_front(PZTList list, void* entry) {

	if (list == NULL || entry == NULL)
		return false;

	bool pop_state = false;
	PZTNode current_entry = NULL;

	if (!list->head)
		goto cleanup;

	current_entry = list->head;
	zt_memcpy(entry, current_entry->value, list->size);

	list->head = current_entry->next;
	if (list->head == NULL)
		list->tail = NULL;

	zt_deallocate(&current_entry->value);
	zt_deallocate(&current_entry);

	zt_atomic_decrement(&list->count);
	pop_state = true;

cleanup:

	return pop_state;

}

bool zt_list_is_empty(PZTList list) {

	if (list == NULL)
		return true;

	long count_value = 0;
	zt_atomic_read(&list->count, &count_value);
	return count_value == 0;

}

/** @brief Removes the first element whose bytes equal *value; false if none matches. */
bool zt_list_remove(PZTList list, void* value) {

	if (list == NULL || value == NULL)
		return false;

	bool    remove_state = false;
	PZTNode node = list->head;
	PZTNode previous = NULL;

	while (node != NULL) {

		if (zt_equal(node->value, value, list->size)) {

			if (previous == NULL)
				list->head = node->next;
			else
				previous->next = node->next;

			if (node == list->tail)         /* the tail moves back, or becomes NULL if the list is now empty */
				list->tail = previous;

			zt_deallocate((void**)&node->value);
			zt_deallocate((void**)&node);

			zt_atomic_decrement(&list->count);     /* same style as push/pop in your list */
			remove_state = true;
			break;
		}

		previous = node;                    /* BEFORE moving on */
		node = node->next;
	}

	return remove_state;

}
long zt_list_count(PZTList list) {

	if (list == NULL)
		return 0;

	long count_value = 0;
	zt_atomic_read(&list->count, &count_value);

	return count_value;
}

