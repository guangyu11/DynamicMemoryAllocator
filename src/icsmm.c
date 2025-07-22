#include "icsmm.h"
#include "debug.h"
#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>

ics_free_header *freelist_head = NULL;
int first = 1;

void *ics_malloc(size_t size) {
    
    //check for error case
    if(size == 0) {
        errno = EINVAL;
        return NULL;
    }

    //first call
    if(first == 1) {
        first = 0;
        void *page = ics_inc_brk();
        ics_footer *prologue = (ics_footer *)page;  //set prologue
        prologue->block_size = 1;
        prologue->fid = FID;
        ics_header *epilogue = (ics_header *)((char *)page + 4088);   //set epilogue
        epilogue->block_size = 1;
        epilogue->hid = HID;
        epilogue->padding_amount = 0;
        ics_free_header *Header = (ics_free_header *)((char *)page + 8);  //set free block
        Header->header.block_size = 4080;
        Header->header.hid = HID;
        Header->header.padding_amount = 0;
        Header->next = NULL;
        Header->prev = NULL;
        ics_footer *Footer = (ics_footer *)((char *)page + 4080);
        Footer->block_size = 4080;
        Footer->fid = FID;
        freelist_head = Header;
        freelist_head->prev = NULL;
    }

    //find free block
    ics_free_header *best = NULL;
    ics_free_header *tmp = freelist_head;
    while(tmp != NULL) {
        if(tmp->header.block_size - 16 >= size) {
            if(best == NULL || best->header.block_size > tmp->header.block_size) {
                best = tmp;
            }
        }
        tmp = tmp->next;

    }
    while(best == NULL) {   //didn't found, extend heap and recheck
        
        void *page = ics_inc_brk();
        if(!page) {
            errno = ENOMEM;
            return NULL;
        }
        ics_footer *prev_footer = (ics_footer *)((char *)page - 16);
        if(prev_footer->block_size % 2 != 0) {  //no coalescing

            ics_header *epilogue = (ics_header *)((char *)page + 4088);   //set epilogue
            epilogue->block_size = 1;
            epilogue->hid = HID;
            epilogue->padding_amount = 0;  
            ics_free_header *Header = (ics_free_header *)((char *)page - 8);  //set free block
            Header->header.block_size = 4096;
            Header->header.hid = HID;
            Header->header.padding_amount = 0;
            Header->next = NULL;
            Header->prev = NULL;
            ics_footer *Footer = (ics_footer *)((char *)page + 4080);
            Footer->block_size = 4096;
            Footer->fid = FID;
            Header->next = freelist_head;
            if(Header->next) {
                Header->next->prev = Header;
            }
            freelist_head = Header;
            freelist_head->prev = NULL;

        } else {    //with coalescing

            ics_header *prev_header = (ics_header *)((char *)prev_footer - prev_footer->block_size + sizeof(ics_footer));
            ics_header *epilogue = (ics_header *)((char *)page + 4088);   //set epilogue
            epilogue->block_size = 1;
            epilogue->hid = HID;
            epilogue->padding_amount = 0; 
            prev_header->block_size += 4096;
            ics_footer *Footer = (ics_footer *)((char *) page + 4080);
            Footer->block_size = prev_footer->block_size + 4096;
            Footer->fid = FID;

        }

        tmp = freelist_head;
        while(tmp != NULL) {
        if(tmp->header.block_size - 16 >= size) {
            if(best == NULL || best->header.block_size > tmp->header.block_size) {
                best = tmp;
            }
        }
        tmp = tmp->next;
        }

    }

    //allocate
    if(best->header.block_size - 16 - size >= 32) {    //need to split

        size_t true_size = size + 16;
        if(size % 16 != 0) {
            true_size += 16-(size % 16);
        }
        if(best->prev) {
            best->prev->next = best->next;
        }
        if(best->next) {
            best->next->prev = best->prev;
        }
        if(best == freelist_head) {
            freelist_head = best->next;
        }

        //split free block
        ics_free_header *new_head = (ics_free_header *)((char *)best + true_size);
        new_head->header.block_size = best->header.block_size - true_size;
        new_head->header.hid = HID;
        new_head->header.padding_amount = 0;
        new_head->next = freelist_head;
        new_head->prev = NULL;
        if(new_head->next) {
            new_head->next->prev = new_head;
        }
        freelist_head = new_head;
        freelist_head->prev = NULL;
        ics_footer *new_footer = (ics_footer *)((char*)new_head + new_head->header.block_size - 8);
        new_footer->fid = FID;
        new_footer->block_size = best->header.block_size - true_size;

        //allocate
        ics_header *allocated = &(best->header);
        allocated->block_size = true_size+1;
        allocated->hid = HID;
        allocated->padding_amount = 16-(size % 16);
        ics_footer *FOOTER = (ics_footer *)((char *)allocated + true_size - sizeof(ics_footer));
        FOOTER->fid = FID;
        FOOTER->block_size = true_size+1;
        return (void *)allocated + sizeof(ics_header);

    } else {    //take the whole block
        
        if(best->prev) {
            best->prev->next = best->next;
        }
        if(best->next) {
            best->next->prev = best->prev;
        }
        if(best == freelist_head) {
            freelist_head = best->next;
        }

        ics_header *allocated = &(best->header);
        allocated->block_size += 1;
        allocated->hid = HID;
        allocated->padding_amount = 16-(size % 16);
        ics_footer *FOOTER = (ics_footer *)((char *)allocated + allocated->block_size - sizeof(ics_footer) - 1);
        FOOTER->fid = FID;
        FOOTER->block_size += 1;
        return (void *)allocated + sizeof(ics_header);

    } 

}


int ics_free(void *ptr) { 

/////////////////////////// Error Check
    if(!ptr) {
        errno = EINVAL;
        return -1;
    }

    ics_free_header *tmp_header = (ics_free_header *)((char *)ptr - sizeof(ics_header));
    ics_footer *tmp_footer = (ics_footer *)((char *)tmp_header + tmp_header->header.block_size - (sizeof(ics_header))-1);

    if(tmp_header->header.hid != HID || tmp_footer->fid != FID) {
        errno = EINVAL;
        return -1;
    }

    if(tmp_header->header.block_size != tmp_footer->block_size) {
        errno = EINVAL;
        return -1;
    }

    if(tmp_header->header.block_size % 2 == 0 || tmp_footer->block_size % 2 == 0) {
        errno = EINVAL;
        return -1;
    }

//////////////////////////

    ics_footer *prev_footer = (ics_footer *)((char *)tmp_header - sizeof(ics_footer));
    if(prev_footer->block_size % 2 != 0) {  //no coalescing
        tmp_header->header.block_size -= 1;
        tmp_footer->block_size -= 1;
        tmp_header->next = freelist_head;
        if(tmp_header->next){
            freelist_head->prev = tmp_header;
        }
        freelist_head = tmp_header;
        tmp_header->prev = NULL;
        return 0;
    }
    //with coalescing
    ics_free_header *prev_header = (ics_free_header *)((char *)prev_footer - prev_footer->block_size + sizeof(ics_footer));
    if(prev_header->next) {
        prev_header->next->prev = prev_header->prev;
    }
    if(prev_header->prev) {
        prev_header->prev->next = prev_header->next;
    }
    if(!prev_header->prev) {
        freelist_head = prev_header->next;
    }
    prev_header->header.block_size += tmp_header->header.block_size - 1;
    tmp_footer->block_size = prev_header->header.block_size;
    prev_header->next = freelist_head;
    if(prev_header->next) {
        prev_header->next->prev = prev_header;
    }
    freelist_head = prev_header;
    prev_header->prev = NULL;
    return 0;

}


void* ics_realloc(void *ptr, size_t size) {
    return (void*) 0xCAFECAFE;
}