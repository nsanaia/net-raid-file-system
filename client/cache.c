#include "cache.h"



void cache_init(struct cache_base * base, int max_len_){
    base->max_size = max_len_ * 1024 * 1024;
    base->log_size = 0;
    base->next = NULL;
}


void prepare_size(struct cache_base * base, size_t size_){
    assert(base != NULL);
    if( size_ >= base->max_size - base->log_size){
        return;
    }
    struct cache_item * curr = base->next;
    struct cache_item * pre = NULL;

    while(curr != NULL){
        if(size_ >= base->max_size - base->log_size){
            return;
        }
        base->next = curr->next;
        base->log_size -= curr->size;
        free(curr->content);
        free(curr);
        curr = base->next;
    }
}



int cache_add(struct cache_base * base,const  char * name_ , off_t offset_, size_t size_, char * content_ ){


    printf(" addddddddd :::::::::::::: %s \n",content_ );

    assert(base != NULL);
    prepare_size(base,size_);
    struct cache_item * curr = base->next;

    struct cache_item  * new_item = malloc(sizeof(struct cache_item ));
    strcpy(new_item->name, name_);
    new_item->size = size_;
    new_item->offset = offset_;
    new_item->content  = malloc(size_);
    new_item->next = NULL;
    memcpy(new_item->content, content_, size_);
    base->next = new_item;

    if(curr == NULL){
        base->next = new_item;
        base->log_size = new_item->size;
        return 1;
    }
    while(1){
        if(curr->next == NULL){
            curr->next = new_item;
            base->log_size += new_item->size;
            return 1;
        }
        curr = curr->next;
    }

    return 0;

}


/*
    return value 
    -1 for cant fin
    1 for find
*/
int cache_find(struct cache_base * base, const char * name_  ,off_t offset_, size_t size_, char * content_ ){
    assert(base != NULL);
    struct cache_item * curr = base->next;
    while(curr != NULL){
        // printf("%s cahce ::::::::::::::\n",name_ );
        // printf("%s cahce ::::::::::::::\n",curr->name );

        // printf("%d cahce ::::::::::::::\n",offset_ );
        // printf("%d cahce ::::::::::::::\n",curr->offset );

        // printf("%d cahce ::::::::::::::\n",size_ );
        // printf("%d cahce ::::::::::::::\n",curr->size );


        if(strcmp(curr->name, name_) == 0 &&  
            curr->offset  <= offset_ && 
            offset_ + size_ <= curr->size + curr->offset) {
            int new_offset = offset_ - curr->offset;
            memcpy(content_, curr->content + new_offset, size_);
            return 1;
            break;
        }
        curr = curr->next;
    }
    return -1;
}


void cache_destroy(struct cache_base * base){
    if(base == NULL){
        return;
    }

    struct cache_item * curr = base->next;
    while(curr != NULL){
        curr = curr->next;
        free(curr->content);
        free(curr);
    }
    free(base);
}


void cache_rename(struct cache_base * base, const char * name_ ){
    assert(base != NULL);
    struct cache_item * curr = base->next;
    while(curr != NULL){
        if(strcmp(name_ , curr->name) == 0){
            strcpy(curr->name , name_);
        }
        curr = curr->next;
    }
}



void cache_remove(struct cache_base * base, const char * name_ ){
    assert(base != NULL);
    struct cache_item * curr = base->next;
    struct cache_item * pre = NULL;
    while(curr != NULL){
        if(strcmp(name_, curr->name) == 0){
            if(pre == NULL){
                base->next = curr->next;
            }else {
                pre->next = curr->next;
            }
            free(curr->content);
            free(curr);
            curr = (pre == NULL) ? base->next : pre->next;
        }else {
            pre = curr;
            curr = curr->next;
        }

    }
}

