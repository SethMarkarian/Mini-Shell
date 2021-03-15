#include "LinkedList.h"

extern void clearLinkedList(LinkedList* ll){
	Node* n = ll -> head;
	Node* nex;
	while(n != NULL){
		nex = n -> next;
		free(n);
		n = nex;
	}
	ll -> head = NULL;
	ll -> tail = NULL;
}

extern void createLinkedList(LinkedList* ll){
	ll -> head = NULL; /*malloc(sizeof(Node));*/
	ll -> tail = NULL; /*malloc(sizeof(Node)); */
	/* unsure if I need to malloc yet */
}

extern void insertInLinkedList(LinkedList* ll, void* p){
	Node* ins = malloc(sizeof(Node));
	ins -> ptr = p;
	ins -> next = NULL;
	if(ll -> tail == NULL){
		ll -> head = ins;
		ll -> tail = ins;
		ins -> prev = NULL;
	}
	else{
		ll -> tail -> next = ins;
		ll -> tail = ins;
	}
}

extern void printLinkedList(LinkedList* ll){
	Node* n = ll -> head;
	while(n != NULL){
		printf("%p, ", n -> ptr);
		n = n -> next;
	}
	printf("\n");
}

extern void removeFromLinkedList(LinkedList* ll, void* p){
	struct Node_tag * n;
	if((n = getNode(ll, p))){
		if(n == (ll -> head) && n == (ll -> tail)){
			(ll -> head) = NULL;
			(ll -> tail) = NULL;
			free(n);
			return;
		}
		if(n == (ll -> head)){
			(ll -> head) = (n -> next);
			(ll -> head -> prev) = NULL;
			free(n);
			return;
		}
		if(n == (ll -> tail)){
			(ll -> tail) = (n -> prev);
			(ll -> tail -> next) = NULL;
			free(n);
			return;
		}
		(n -> prev -> next) = (n -> next);
		(n -> next -> prev) = (n -> prev);
		free(n);
		return;
	}
	return;
}

struct Node* getNode(LinkedList* ll, void* p){
	struct Node_tag * now = (ll -> head);
	while(now){
		if((now -> ptr) == p)
			return now;
		now = (now -> next);
	}
	return NULL;
}
