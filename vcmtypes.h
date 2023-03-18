typedef struct VCMREF {
	char** ref; //reference to a user pointer of the container
	struct VCMREF* next; //next reference if there are more than one
} VCMREF;
typedef struct VCMCC {
	size_t index; //index of the container in the array of containers
	char** pointer; //pointer to the container
	VCMREF* refs; //reference to the container
	size_t size; //size of the container
}VCMCC;
typedef struct VCMHCC {
	size_t c_num; //the number of container currently allocated
	size_t c_max; //the maximum number of container that can be allocated
	size_t pfns_num; //the number of page frame number currently allocated
	size_t pfns_max; //the maximum number of page frame number that can be allocated

	char** pointer_storage; // storage for container pointers
	VCMREF* ref_storage; // storage for container references allocated using HeapAlloc and grows using HeapReAlloc
	size_t last_c_size; // the size of the last container allocated

	size_t* pfns_storage; // storage for page frame numbers allocated using HeapAlloc and grows using HeapReAlloc

} VCMHCC;
typedef struct VCMHCM {
	size_t hc_num; //the number of HCC currently allocated
	size_t hc_max; //the maximum number of HCC that can be allocated
	VCMHCC* hcc_storage; //storage for HCC allocated using HeapAlloc and grows using HeapReAlloc
	char** p_storage; //storage for pointers of hyper containers allocated using HeapAlloc and grows using HeapReAlloc
	size_t last_hc_size; //the size of the last HCC allocated
} VCMHCM;
typedef struct VCMHCA {
	VCMHCM* used_hcm;
	size_t hc_size;
	size_t c_per_hc;
}VCMHCA;

typedef struct VCMTHM {
	VCMHCM hcm;
	unsigned long* thid;
	size_t hc_size_th;
	size_t c_num_th;
}VCMTHC;

typedef struct VCMMEM{
	size_t userdata;
	size_t vcmdata;
	size_t total;
}VCMMEM;