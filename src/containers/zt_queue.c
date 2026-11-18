#include <windows.h>

#include "memory/zt_memory.h"
#include "containers/zt_list.h"
#include "containers/zt_queue.h"
typedef PZTList PZTQueue;

/** @brief Creates an empty queue of fixed-size elements; *queue must be NULL on entry. */
bool zt_queue_init(PZTQueue* queue, size_t size) {

	if (queue == NULL || *queue != NULL || size == 0)
		return false;

	bool   init_state = false;
	PZTList temp_list = NULL;
	if (!zt_list_init(&temp_list, size))
		goto cleanup;

	*queue = temp_list;
	init_state = true;

cleanup:
	return init_state;

}

/** @brief Frees every remaining element and the queue itself. */
bool zt_queue_deinit(PZTQueue* queue) {

	if (queue == NULL || *queue == NULL)
		return false;

	return zt_list_deinit(queue);

}

/** @brief Appends a copy of *value (element size bytes) at the back. */
bool zt_queue_enqueue(PZTQueue queue, void* value) {

	if (queue == NULL || value == NULL)
		return false;

	return zt_list_push_back(queue, value);

}

/** @brief Copies the oldest element into out_value and removes it; false if empty. */
bool zt_queue_dequeue(PZTQueue queue, void* out_value) {

	if (queue == NULL || out_value == NULL)
		return false;

	return zt_list_pop_front(queue, out_value);
}

/** @brief Number of stored elements; 0 for a NULL queue. */
long zt_queue_count(PZTQueue queue) {

	if (queue == NULL)
		return 0;

	return zt_list_count(queue);
}

/** @brief True if there are no elements; a NULL queue counts as empty. */
bool zt_queue_is_empty(PZTQueue queue) {
	if (queue == NULL)
		return true;

	return zt_list_is_empty(queue);

}
//typedef struct Node {
//	struct Node* next;
//	void* value;
//} Node, * PNode;
//
//typedef struct ZTQueue {
//	PNode			head;
//	PNode			tail;
//	size_t			size;
//	volatile long count;
//} ZTQueue, * PZTQueue;
//
///** @brief Creates an empty queue of fixed-size elements; *queue must be NULL on entry. */
//bool zt_queue_init(PZTQueue* queue, size_t size) {
//
//	if (queue == NULL || *queue != NULL || size == 0)
//		return false;
//
//	bool   init_state = false;
//	PZTQueue temp_queue = NULL;
//	if (!zt_allocate(&temp_queue, sizeof(ZTQueue)))
//		goto cleanup;
//
//	zt_memset(temp_queue, 0, sizeof(ZTQueue));
//
//	temp_queue->head = NULL;
//	temp_queue->tail = NULL;
//	temp_queue->size = size;
//	temp_queue->count = 0;
//
//	*queue = temp_queue;
//	init_state = true;
//
//cleanup:
//	return init_state;
//
//}
//
///** @brief Frees every remaining element and the queue itself. */
//bool zt_queue_deinit(PZTQueue* queue) {
//
//	if (queue == NULL || *queue == NULL)
//		return false;
//
//	PNode node = (*queue)->head;
//	while (node != NULL) {
//
//		PNode next = node->next;     
//		zt_deallocate(&node->value);
//		zt_deallocate(&node);
//
//		node = next;
//	}
//
//	return zt_deallocate(queue);
//
//}
//
///** @brief Appends a copy of *value (element size bytes) at the back. */
//bool zt_queue_enqueue(PZTQueue queue, void* value) {
//
//	if (queue == NULL || value == NULL)
//		return false;
//
//	bool enqueue_state = false;
//	PNode temp_value   = NULL;
//	if (!zt_allocate(&temp_value, sizeof(Node)))
//		goto cleanup;
//
//	if (!zt_allocate(&temp_value->value, queue->size))
//		goto cleanup;
//
//	temp_value->next = NULL;
//	zt_memset(temp_value->value, 0, queue->size);
//	zt_memcpy(temp_value->value, value, queue->size);
//
//	if (queue->tail)
//		queue->tail->next = temp_value;
//	else
//		queue->head = temp_value;
//
//	queue->tail = temp_value;
//
//	InterlockedIncrement(&queue->count);
//	enqueue_state = true;
//
//cleanup:
//
//	if (!enqueue_state && temp_value)
//		zt_deallocate(&temp_value);
//
//	return enqueue_state;
//
//}
//
///** @brief Copies the oldest element into out_value and removes it; false if empty. */
//bool zt_queue_dequeue(PZTQueue queue, void* out_value) {
//
//	if (queue == NULL || out_value == NULL)
//		return false;
//
//	bool dequeue_state = false;
//	PNode current_node = NULL;
//
//	if (!queue->head)
//		goto cleanup;
//
//	current_node = queue->head;
//	zt_memcpy(out_value, current_node->value, queue->size);
//
//	queue->head = current_node->next;
//	if (queue->head == NULL)
//		queue->tail = NULL;
//
//	zt_deallocate(&current_node->value);
//	zt_deallocate(&current_node);
//
//	InterlockedDecrement(&queue->count);
//	dequeue_state = true;
//
//cleanup:
//
//	return dequeue_state;
//
//}
//
///** @brief Number of stored elements; 0 for a NULL queue. */
//long zt_queue_count(PZTQueue queue) {
//
//	if (queue == NULL)
//		return 0;
//
//	return InterlockedCompareExchange(&queue->count, 0, 0);
//}
//
///** @brief True if there are no elements; a NULL queue counts as empty. */
//bool zt_queue_is_empty(PZTQueue queue) {
//	if (queue == NULL)
//		return true;
//
//	return InterlockedCompareExchange(&queue->count, 0, 0) == 0;
//
//}