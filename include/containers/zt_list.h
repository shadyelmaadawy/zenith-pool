#ifndef ZT_LIST_H
#define ZT_LIST_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Singly-linked list of fixed-size elements, stored by copy; opaque.
 *
 * Every element is a byte copy of exactly the size given to zt_list_init().
 * Used as a building block:
 *   - Queue (FIFO): zt_list_push_back() + zt_list_pop_front()
 *   - Stack (LIFO): zt_list_push_front() + zt_list_pop_front()
 */
typedef struct ZTList ZTList;
typedef ZTList* PZTList;

/**
 * @brief Creates an empty list.
 *
 * @param[out] list Pointer to a variable that must hold NULL. Receives the new list on success; left untouched on failure.
 * @param[in]  size Size in bytes of every element. Fixed for the life of the list. Must be greater than 0.
 *
 * @return true on success; false if @p list is NULL, *@p list is not NULL, @p size is 0, or allocation failed.
 *
 * @note Release with zt_list_deinit().
 */
bool zt_list_init(PZTList* list, size_t size);

/**
 * @brief Releases the list and every element still inside it, then nulls the pointer.
 *
 * @param[in,out] list Pointer to the variable holding the list. Set to NULL on success.
 *
 * @return true on success; false if @p list or *@p list is NULL, or the final release failed.
 *
 * @note Elements are plain byte copies: anything they point to (tasks, handles, buffers) is NOT released. Pop and clean those up first.
 */
bool zt_list_deinit(PZTList* list);

/**
 * @brief Appends a copy of an element at the back.
 *
 * @param list  List from zt_list_init().
 * @param entry Pointer to the element. Exactly the list's element size is copied from it.
 *
 * @return true if stored; false if @p list or @p entry is NULL, or allocation failed. On failure the list is unchanged.
 */
bool zt_list_push_back(PZTList list, void* entry);

/**
 * @brief Inserts a copy of an element at the front.
 *
 * @param list  List from zt_list_init().
 * @param entry Pointer to the element. Exactly the list's element size is copied from it.
 *
 * @return true if stored; false if @p list or @p entry is NULL, or allocation failed. On failure the list is unchanged.
 */
bool zt_list_push_front(PZTList list, void* entry);

/**
 * @brief Removes the last element and copies it out.
 *
 * @param[in]  list  List from zt_list_init().
 * @param[out] entry Buffer of at least the list's element size that receives the element.
 *
 * @return true if an element was removed; false if @p list or @p entry is NULL, or the list is empty. On failure @p entry is left untouched.
 *
 * @note O(n): a singly-linked list must walk to the node before the last. For a stack, use zt_list_push_front() with zt_list_pop_front() instead.
 */
bool zt_list_pop_back(PZTList list, void* out_entry);

/**
 * @brief Removes the first element and copies it out.
 *
 * @param[in]  list  List from zt_list_init().
 * @param[out] entry Buffer of at least the list's element size that receives the element.
 *
 * @return true if an element was removed; false if @p list or @p entry is NULL, or the list is empty. On failure @p entry is left untouched.
 */
bool zt_list_pop_front(PZTList list, void* out_entry);

/** @brief Returns true if the list holds no elements, or if @p list is NULL. */
bool zt_list_is_empty(PZTList list);

bool zt_list_remove(PZTList list, void* value);

/** @brief Returns the number of elements currently stored; 0 if @p list is NULL. */
long zt_list_count(PZTList list);

#endif /* ZT_LIST_H */