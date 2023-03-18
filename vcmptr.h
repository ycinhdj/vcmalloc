#pragma once

extern "C" {
#include "vcmalloc.h"
}


template<class T>
class VCMPTR
{
private:
    T pointer;
    
public:
    VCMPTR() : pointer{ 0 } {}
    VCMPTR(const VCMPTR<T>& ptrCopy)
    {
        T ptr = *(T*)&ptrCopy;
        
        size_t hc_i;
        size_t c_i;

        hc_i = hcm_search_c(&default_hcm, (char*)ptr);
        if (hc_i != -1) {
            c_i = hcc_search_c(&(default_hcm.hcc_storage[hc_i]), (char*)ptr);
            vcmref_insert(&default_hcm.hcc_storage[hc_i].ref_storage[c_i], (char**)&pointer);
        }
        pointer = ptr;
    }
    
    VCMPTR(T& ptr)
    {
        size_t hc_i;
        size_t c_i;

        hc_i = hcm_search_c(&default_hcm, (char*)ptr);
        if (hc_i != -1) {
            c_i = hcc_search_c(&(default_hcm.hcc_storage[hc_i]), (char*)ptr);
            vcmref_insert(&default_hcm.hcc_storage[hc_i].ref_storage[c_i], (char**)&pointer);
        }
        pointer = ptr;
    }
    ~VCMPTR()
    {
        size_t hc_i;
        size_t c_i;

        hc_i = hcm_search_c(&default_hcm, (char*)pointer);
        if (hc_i != -1) {
            c_i = hcc_search_c(&(default_hcm.hcc_storage[hc_i]), (char*)pointer);
            vcmref_remove(&default_hcm.hcc_storage[hc_i].ref_storage[c_i], (char**)this);
        }
    }
    
    void operator=(VCMPTR<T> ptrCopy)
    {
        T ptr = *(T*)&ptrCopy;
        size_t hc_i;
        size_t c_i;

        hc_i = hcm_search_c(&default_hcm, (char*)ptr);
        if (hc_i != -1) {
            c_i = hcc_search_c(&(default_hcm.hcc_storage[hc_i]), (char*)ptr);
            vcmref_insert(&default_hcm.hcc_storage[hc_i].ref_storage[c_i], (char**)&pointer);
        }

        hc_i = hcm_search_c(&default_hcm, (char*)pointer);
        if (hc_i != -1) {
            c_i = hcc_search_c(&(default_hcm.hcc_storage[hc_i]), (char*)pointer);
            vcmref_remove(&default_hcm.hcc_storage[hc_i].ref_storage[c_i], (char**)&pointer);
        }

        pointer = ptr;
    }
    void operator=(T ptr) {

        size_t hc_i;
        size_t c_i;

        hc_i = hcm_search_c(&default_hcm, (char*)ptr);
        if (hc_i != -1) {
            c_i = hcc_search_c(&(default_hcm.hcc_storage[hc_i]), (char*)ptr);
            vcmref_insert(&default_hcm.hcc_storage[hc_i].ref_storage[c_i], (char**)&pointer);
        }

        hc_i = hcm_search_c(&default_hcm, (char*)pointer);
        if (hc_i != -1) {
            c_i = hcc_search_c(&(default_hcm.hcc_storage[hc_i]), (char*)pointer);
            vcmref_remove(&default_hcm.hcc_storage[hc_i].ref_storage[c_i], (char**)&pointer);
        }

        pointer = ptr;
    }
    
    bool operator== (VCMPTR<T>& other) {
        return pointer == other.pointer;
    }
    bool operator== (T& other) {
        return pointer == other;
    }

    bool operator!= (VCMPTR<T>& other) {
        return pointer != other.pointer;
    }
    bool operator!= (T& other) {
        return pointer != other;
    }
    
    decltype(*pointer) operator*() const
    {
        return *pointer;
    }
    T operator->() const
    {
        return pointer;
    }
    

	T operator+(int i)
	{
		return pointer + i;
	}
    T operator+= (int i){
        return pointer += i;
    }
    T operator++(int)
    {
		T temp = pointer;
        ++pointer;
        return temp;
    }
    T operator++()
    {
        return ++pointer;
    }
    
    T operator-(int i) {
        return pointer - i;
    }
    T operator-= (int i)
    {
        return pointer -= i;
    }
    T operator--(int)
    {
        T temp = pointer;
        --pointer;
        return temp;
    }
    T operator--() {
        return --pointer;
    }

    operator T() const{
        return pointer;
    }


    T get() {
        return pointer;
    }


};
