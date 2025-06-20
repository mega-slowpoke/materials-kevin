#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gradebook.h"

// This is the (somewhat famous) djb2 hash
unsigned hash(const char *str) {
    unsigned hash_val = 5381;
    int i = 0;
    while (str[i] != '\0') {
        hash_val = ((hash_val << 5) + hash_val) + str[i];
        i++;
    }
    return hash_val % NUM_BUCKETS;
}

gradebook_t* create_gradebook(const char* class_name) {
    // TODO: craete a new book
    gradebook_t* book = malloc(sizeof(gradebook_t));
    if (book == NULL) {
        return NULL;
    }
    // initialize the gradebook
    for (int i = 0; i < NUM_BUCKETS; i++) {
        book->buckets[i] = NULL;
    }
    // strcpy book->class_name
    strcpy(book->class_name, class_name);
    return book;
}

const char* get_gradebook_name(const gradebook_t* book) {
    return book->class_name;
}

int add_score(gradebook_t* book, const char* name, int score) {
    //if book or name doesn't exist, return false
    if (book == NULL || name == NULL) {
        return -1;
    }
    //if the name exists, renew the score
    unsigned idx = hash(name);
    node_t* curr = book->buckets[idx];
    //refer to the score, change the score
    while (name != NULL) {
        if (strcmp(curr->name, name) == 0) {
            curr->score = score;
            return 0;
        }
        curr = curr->next; //traverse the list to find the corresponding name
    }
    return 0;
}

int find_score(const gradebook_t* book, const char* name) {
    if (book == NULL || name == NULL) {
        return -1;
    }
    
    unsigned idx = hash(name);
    node_t* curr = book->buckets[idx];

    while (curr != NULL) {
        if (strcmp(curr->name, name) == 0) {
            return curr->score;
        }
        curr = curr->next;
    }
    return -1;
}

void print_gradebook(const gradebook_t *book) {
    printf("%s\n", book->class_name);
    for (int i = 0; i < NUM_BUCKETS; i++) {
        node_t* curr = book->buckets[i];
        while (curr != NULL) {
            printf("Student: %s, Score: %d\n", curr->name, curr->score);
            curr = curr->next;
        }
    }
}

void free_gradebook(gradebook_t *book) {
    if (book == NULL) {
        return;
    }
    for (int i = 0; i < NUM_BUCKETS; i++) {
        node_t* curr = book->buckets[i];
        while (curr != NULL) {
            node_t* temp = curr;
            curr = curr->next;
            free(temp);
        }
    }
    free(book);
}

int write_gradebook_to_text(const gradebook_t *book) {
    char file_name[MAX_NAME_LEN + strlen(".txt")];
    strcpy(file_name, book->class_name);
    strcat(file_name, ".txt");
    FILE *f = fopen(file_name, "w");
    if (f == NULL) {
        return -1;
    }

    fprintf(f, "%u\n", book->size);
    for (int i = 0; i < NUM_BUCKETS; i++) {
        node_t *current = book->buckets[i];
        while (current != NULL) {
            fprintf(f, "%s %d\n", current->name, current->score);
            current = current->next;
        }
    }
    fclose(f);
    return 0;
}

gradebook_t *read_gradebook_from_text(const char *file_name) {
    char book_name[MAX_NAME_LEN];
    strncpy(book_name, file_name, strlen(file_name) - strlen(".txt"));
    // remember to append '\0' at the end of book_name
    book_name[strlen(file_name) - strlen(".txt")] = '\0';
    gradebook_t *new_book = create_gradebook(book_name);
    // malloc failed
    if (new_book == NULL) {
        return NULL;
    }
    
    FILE *pf = fopen(file_name, "r");
    // read failed
    if (pf == NULL) {
        // remember to free new_book
        free(new_book);
        return NULL;
    }
    fscanf(pf, "%d", &(new_book->size));
    char name[MAX_NAME_LEN] = {0};
    int score = -1;
    for (int i = 0; i < new_book->size; i++) {	
        fscanf(pf, "%s %d", name, &score);
        add_score(new_book, name, score);
    }
    fclose(pf);
    pf = NULL;
    return new_book;
}
