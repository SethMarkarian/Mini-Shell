#ifndef LINKEDLIST
#define LINKEDLIST

#include <stdio.h>
#include <stdlib.h>

typedef struct Node_tag {
	void* ptr;
	struct Node_tag * next;
	struct Node_tag * prev;
} Node;

typedef struct{
	Node* head;
	Node* tail;
} LinkedList;

extern void createLinkedList(LinkedList* ll);
extern void insertInLinkedList(LinkedList* ll, void* p);
extern void printLinkedList(LinkedList* ll);
extern void clearLinkedList(LinkedList* ll);
extern void removeFromLinkedList(LinkedList* ll, void* p);
struct Node* getNode(LinkedList* ll, void* p);

#endif
