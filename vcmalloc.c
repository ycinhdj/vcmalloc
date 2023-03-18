#include "windows.h"
#include "stdio.h"
#include "math.h"
#include <time.h>
#include <omp.h>
#include "vcmtypes.h"
#include "stdbool.h"

SYSTEM_INFO	system_info = { 0 };
size_t page_size = 0;

void vcm_global_init() {

	TOKEN_PRIVILEGES token_priviledges = {
		.PrivilegeCount = 1,
		.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED
	};

	HANDLE Token;

	if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token))
		if (LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &(token_priviledges.Privileges[0].Luid)))
			AdjustTokenPrivileges(Token, FALSE, &token_priviledges, 0, NULL, NULL);

	if (GetLastError() != ERROR_SUCCESS) {
		printf("vcm_global_init: AdjustTokenPrivileges failed");
		while (1);
	}

	CloseHandle(Token);

	GetSystemInfo(&system_info);
	page_size = system_info.dwPageSize;
}
size_t MSB(size_t n) {
	size_t msb = 1;
	while (msb <= n)
		msb <<= 1;
	msb >>= 1;
	return msb;
}

CRITICAL_SECTION CriticalSection;

void vcmref_insert(VCMREF* ref, char** new_ref) {
	


	if (!ref->ref) {
		ref->ref = new_ref;
		return;
	}
	
	if (!ref->next) {
		ref->next = (VCMREF*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(VCMREF));
		ref->next->ref = ref->ref;
		ref->ref = new_ref;
		return;
	}

	VCMREF* temp_ref = (VCMREF*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(VCMREF));
	*temp_ref = *ref;

	ref->next = temp_ref;
	ref->ref = new_ref;
}
void vcmref_remove(VCMREF* ref, char** delete_ref) {
	VCMREF* i = ref;
	while (i) {
		if (i->ref == delete_ref) {
			if (i->next) {
				VCMREF* temp = i->next;
				*i = *temp;
				HeapFree(GetProcessHeap(), 0, temp);
				return;
			}
			if (i != ref)
				HeapFree(GetProcessHeap(), 0, i);
			else i->ref = 0;
			return;
		}
		i = i->next;
	}
}
void vcmref_free(VCMREF* ref) {
	VCMREF* i = ref->next;
	while (i) {
		VCMREF* temp = i;
		i = i->next;
		HeapFree(GetProcessHeap(), 0, temp);
	}
}
void vcmref_shift(VCMREF *ref, __int64 steps) {
	VCMREF* i = ref;
	while (i && i->ref) {
		*i->ref += steps;
		i = i->next;
	}
}

void	cc_init(VCMCC* cc, size_t index, char** pointer, size_t size, VCMREF* refs) {
	cc->index = index;
	cc->pointer = pointer;
	cc->size = size;
	cc->refs = refs;
}
size_t	cc_offset(VCMCC* cc) {
	return (size_t)*cc->pointer & (page_size - 1);
}
char*	cc_mapping_address(VCMCC* cc) {
	if (cc_offset(cc))
		return (char*)((size_t)*cc->pointer & ~(page_size - 1)) + page_size;
	else
		return  *cc->pointer;
}

char*	cc_next_address(VCMCC* cc) {
	return *cc->pointer + cc->size;
}
char*	cc_next_mapping_address(VCMCC* cc) {
	size_t last_page_offset = (size_t)cc_next_address(cc) & (page_size - 1);
	if (last_page_offset)
		return (char*)((size_t)cc_next_address(cc) & ~(page_size - 1)) + page_size;
	else
		return cc_next_address(cc);
}
size_t	cc_last_page_offset(VCMCC* cc) {
	return (size_t)cc_next_address(cc) & (page_size - 1);
}
size_t	cc_num_of_pages(VCMCC* cc) {
	return (cc_next_mapping_address(cc) - cc_mapping_address(cc)) / page_size;
}
size_t	cc_guest_bytes(VCMCC* cc) {
	return (size_t)cc_next_mapping_address(cc) - (size_t)cc_next_address(cc);
}
size_t	cc_prior_free_bytes(VCMCC* cc) {
	return cc_mapping_address(cc) - *cc->pointer;
}

void	hcc_init(VCMHCC* hcc)
{
	hcc->pfns_num = 0;
	hcc->c_num = 0;

	size_t max = page_size / sizeof(size_t);
	hcc->pfns_max = max;
	hcc->c_max = max;

	hcc->pointer_storage = (char**)HeapAlloc(GetProcessHeap(), 0, max * sizeof(void*));
	hcc->ref_storage = (VCMREF*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, max * sizeof(VCMREF));
	hcc->last_c_size = 0;
	hcc->pfns_storage = (size_t*)HeapAlloc(GetProcessHeap(), 0, max * sizeof(size_t));

};
void	hcc_init_fit(VCMHCC* hcc, size_t fit_pfns, size_t fit_c)
{
	hcc->pfns_num = 0;
	hcc->c_num = 0;
	
	hcc->pfns_max = fit_pfns;
	hcc->c_max = fit_c;
	hcc->pointer_storage = (char**)HeapAlloc(GetProcessHeap(), 0, hcc->c_max * sizeof(void*));
	hcc->ref_storage = (VCMREF*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, hcc->c_max * sizeof(VCMREF));
	hcc->last_c_size = 0;
	hcc->pfns_storage = (size_t*)HeapAlloc(GetProcessHeap(), 0, hcc->pfns_max * sizeof(size_t));
	
	//check is allocation is sucessfull
	if (!hcc->pointer_storage || !hcc->ref_storage || !hcc->pfns_storage) {
		//print error
		printf("hcc_init_fit: storage allocation failed\n");
		while (1);
	}
};
size_t	hcc_cc_size(VCMHCC* hcc, size_t index) {
	if (index == hcc->c_num - 1)
		return hcc->last_c_size;
	return hcc->pointer_storage[index + 1] - hcc->pointer_storage[index];
}
VCMCC	hcc_getcc(VCMHCC* hcc, size_t index) {
	VCMCC cc;
	cc.index = index;
	cc.pointer = &hcc->pointer_storage[index];
	cc.refs = &hcc->ref_storage[index];
	cc.size = hcc_cc_size(hcc, index);
	return cc;
}
void	hcc_fit_pfns(VCMHCC* hcc, size_t pfns_num) {

	if (hcc->pfns_max < pfns_num) {
		//print the pfns_num and the function name
		printf("hcc_fit_pfns : %zd\n", pfns_num);

		size_t new_pfns_max = hcc->pfns_max;

		while (new_pfns_max < pfns_num)
			new_pfns_max *= 2;
		
		hcc->pfns_storage = (size_t*)HeapReAlloc(GetProcessHeap(), 0, hcc->pfns_storage, new_pfns_max * sizeof(size_t));
		if (!hcc->pfns_storage) {
			printf("hcc_fit_pfns : couldn't realloc pfns_storage");
			while (1);
		}

		hcc->pfns_max = new_pfns_max;

	}
}
void	hcc_fit_c(VCMHCC* hcc, size_t cc_num) {
	if (hcc->c_max < cc_num) {
		//print the cc_num and the function name
		printf("hcc_fit_cc : %zd\n", cc_num);
		
		size_t newMax = hcc->c_max;

		while (newMax < cc_num)
			newMax *= 2;

		hcc->pointer_storage = (char**)HeapReAlloc(GetProcessHeap(), 0, hcc->pointer_storage, newMax * sizeof(char*));
		hcc->ref_storage = (VCMREF*)HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, hcc->ref_storage, newMax * sizeof(VCMREF));

		if (!hcc->pointer_storage || !hcc->ref_storage) {
			printf("hcc_fit_cc: couldn't realloc cc storage");
			while (1);
		}

		hcc->c_max = newMax;
	}
}
size_t	hcc_pfns_position(VCMHCC* hcc, size_t i_start, size_t i_end) {
	size_t pfns_pos = hcc->pointer_storage[i_end] - hcc->pointer_storage[i_start];
	pfns_pos = (size_t)ceil((long double)pfns_pos / page_size);
	return pfns_pos;
}
size_t	hcc_search(VCMHCC* hcc, char* p)
{
	size_t max = hcc->c_num - 1;
	size_t msb = MSB(max);
	size_t index = msb;
	
	while (hcc->pointer_storage[index] < p) {
		msb = MSB(max - index);
		if (msb == 0)
			return -1;
		index += msb;
	}
	while (1) {
		char* pointer = hcc->pointer_storage[index];

		if (pointer == p)
			return index;
		else if (pointer < p)
			index += msb;
		else
			index -= msb;

		if (msb == 0)
			return -1;

		msb /= 2;
	}
	return index;
};
size_t	hcc_search_c(VCMHCC* hcc, char* p) {
	size_t max = hcc->c_num - 1;
	size_t msb = MSB(max);
	size_t index = msb;

	while (hcc->pointer_storage[index] < p) {
		msb = MSB(max - index);
		if (msb == 0) {
			if (hcc->pointer_storage[index] + hcc->last_c_size <= p)
				return -1;
			else
				return index;
		}
		index += msb;
	}

	while (1) {

		if (hcc->pointer_storage[index] == p)
			return index;

		if (p < hcc->pointer_storage[index])
			index -= msb;

		else {
			if (index == max && hcc->pointer_storage[index] + hcc->last_c_size <= p)
				return index;
			if (p < hcc->pointer_storage[index + 1])
				return index;
			index += msb;
		}

		if (msb == 0)
			return -1;

		msb /= 2;
	}

}

void	hcm_init(VCMHCM* hcm)
{
	if (!page_size) vcm_global_init();

	hcm->hc_max = (INT64)(page_size / sizeof(VCMHCC));
	hcm->hcc_storage = (VCMHCC*)HeapAlloc(GetProcessHeap(), 0, hcm->hc_max * sizeof(VCMHCC));
	hcm->p_storage = (char**)HeapAlloc(GetProcessHeap(), 0, hcm->hc_max * sizeof(char*));
	hcm->hc_num = 0;
	hcm->last_hc_size = 0;
};
void	hcm_init_fit(VCMHCM* hcm, size_t hc_max)
{
	if (!page_size) vcm_global_init();

	hcm->hc_max = hc_max;
	hcm->hcc_storage = (VCMHCC*)HeapAlloc(GetProcessHeap(), 0, hcm->hc_max * sizeof(VCMHCC));
	hcm->p_storage = (char**)HeapAlloc(GetProcessHeap(), 0, hcm->hc_max * sizeof(char*));
	hcm->hc_num = 0;
	hcm->last_hc_size = 0;
};

void	hcm_fit(VCMHCM* hcm, size_t max) {
	if (hcm->hc_max < max) {
		printf("hcm_fit\n");
		size_t new_max = hcm->hc_max;
		while (new_max < max)
			new_max *= 2;
		size_t hcc_per_page = (INT64)(page_size / sizeof(VCMHCC));
		hcm->hcc_storage = (VCMHCC*)HeapReAlloc(GetProcessHeap(), 0, hcm->hcc_storage, (new_max) * sizeof(VCMHCC));
		hcm->p_storage = (char**)HeapReAlloc(GetProcessHeap(), 0, hcm->p_storage, (new_max) * sizeof(PVOID));
		if (!hcm->hcc_storage || !hcm->p_storage) {
			printf("hcm_fit :: HeapReAlloc failed");
			while (1);
		}
		hcm->hc_max = new_max;
	}
}

size_t	hcm_search_hc(VCMHCM* hcm, char* p)
{
	size_t max = hcm->hc_num - 1;
	size_t msb = MSB(max);
	size_t index = msb;

	while (hcm->p_storage[index] < p)
	{
		msb = MSB(max - index);
		if (msb == 0)
			return -1;
		index += msb;
	}


	while (1) {

		char* pointer = hcm->p_storage[index];

		if (pointer == p)
			return index;
		else if (pointer < p)
			index += msb;
		else
			index -= msb;

		if (msb == 0)
			return -1;

		msb /= 2;

	}

	return index;
}
size_t	hcm_search_c(VCMHCM* hcm, char* p) {
	if (hcm->hc_num == 0)
		return -1;
	size_t max = hcm->hc_num - 1;
	size_t msb = MSB(max);
	size_t index = msb;

	while (hcm->p_storage[index] < p) {
		msb = MSB(max - index);
		if (msb == 0) {
			if (hcm->p_storage[index] + hcm->last_hc_size <= p)
				return -1;
			else
				return index;
		}
		index += msb;
	}

	while (1) {

		if (hcm->p_storage[index] == p)
			return index;

		//if (hcm->p_storage[index] < p) {
		//	if (index == max)
		//		return index;
		//	if (p < hcm->p_storage[index + 1])
		//		return index;
		//}

		if (p < hcm->p_storage[index])
			index -= msb;

		else {
			if (index == max && hcm->p_storage[index] + hcm->last_hc_size <= p)
				return index;
			if (p < hcm->p_storage[index + 1])
				return index;
			index += msb;
		}
		
		if (msb == 0)
			return -1;

		msb /= 2;
	}

}

char*	hcm_virtualalloc(VCMHCM* hcm, size_t* hc_size) {

	size_t size = *hc_size;

	SYSTEM_INFO sysInf; GetSystemInfo(&sysInf); DWORD aG = sysInf.dwAllocationGranularity;

	char* requestedAddress = 0;
	char* lpMemReserved = 0;

	if (!hcm->hc_num) {

		lpMemReserved = (char*)VirtualAlloc(requestedAddress, size, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);

		size = (size_t)ceil((double)size / aG);
		size *= aG;

		*hc_size = size;

		return lpMemReserved;
	}


	PVOID lastAddress = hcm->p_storage[hcm->hc_num - 1];
	requestedAddress = (char*)lastAddress + hcm->last_hc_size;


	while (!lpMemReserved) {
		requestedAddress += aG;
		lpMemReserved = (char*)VirtualAlloc(requestedAddress, size, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);
	}

	size = (size_t)ceil((double)size / aG);
	size *= aG;

	*hc_size = size;

	return lpMemReserved;

}

void	hcm_insert_hc(VCMHCM* hcm, size_t size)
{
	hcm_fit(hcm, hcm->hc_num + 1);
	hcm->p_storage[hcm->hc_num] = hcm_virtualalloc(hcm, &size);
	hcm->last_hc_size = size;
	VCMHCC* hcc = &hcm->hcc_storage[hcm->hc_num];
	hcc_init(hcc);
	hcm->hc_num++;
}
void	hcm_insert_hc_fit(VCMHCM* hcm, size_t size, size_t fit_pfns, size_t fit_c)
{
	hcm_fit(hcm, hcm->hc_num + 1);
	hcm->p_storage[hcm->hc_num] = hcm_virtualalloc(hcm, &size);
	hcm->last_hc_size = size;
	VCMHCC* hcc = &hcm->hcc_storage[hcm->hc_num];
	hcc_init_fit(hcc, fit_pfns, fit_c);
	hcm->hc_num++;
}
char*	hcm_insert_c(VCMHCM* hcm, size_t hcc_index, size_t size) {
	
	VCMHCC* hcc = &hcm->hcc_storage[hcc_index];
	void* hca = hcm->p_storage[hcc_index];

	char* mapping_address = 0;
	char* next_addr = 0;
	size_t requested_bytes = size;
	size_t requestedPages = (size_t)ceil((double)requested_bytes / page_size);


	if (!hcc->c_num) {
		mapping_address = hca;
		next_addr = hca;
	}
	else {
		VCMCC last_cc = hcc_getcc(hcc, hcc->c_num - 1);
		size_t last_guest_bytes = cc_guest_bytes(&last_cc);

		mapping_address = cc_next_mapping_address(&last_cc);
		next_addr = cc_next_address(&last_cc);

		if (requested_bytes > last_guest_bytes)
			requestedPages = (size_t)ceil((double)(requested_bytes - last_guest_bytes) / page_size); //https://stackoverflow.com/a/65784221/10772859
		else
			requestedPages = 0;
	}

	if (requestedPages) {
		hcc_fit_pfns(hcc, hcc->pfns_num + requestedPages);
		PULONG_PTR PFNs = hcc->pfns_storage + hcc->pfns_num;
		size_t allocatedPFNs = requestedPages;

		if (!AllocateUserPhysicalPages(GetCurrentProcess(), &allocatedPFNs, PFNs))
		{
			printf("hcm_insert_cc: Physical Memory Allocation Error , Probably Physical Memory Unavailable");
			while (1);
		}

		if (allocatedPFNs != requestedPages)
		{
			printf("hcm_insert_cc: Physical Memory Allocation Incomplete , Probably Physical Memory Unavailable");
			while (1);
		}

		if (requestedPages)
			if (!MapUserPhysicalPages(mapping_address, requestedPages, PFNs))
			{
				printf("hcm_insert_cc: MapUserPhysicalPages failed");
				while (1);
			}

		hcc->pfns_num += requestedPages;
	}

	size_t i = hcc->c_num;
	hcc_fit_c(hcc, ++hcc->c_num);

	hcc->pointer_storage[i] = next_addr;
	hcc->last_c_size = size;

	return next_addr;
};
void	hcm_insert_c_r(VCMHCM* hcm, char* hca, char** userpointer, size_t size) {

	size_t hcc_index = hcm_search_hc(hcm, hca); if (hcc_index == -1) { printf("hcm_insert_cc: hypercontainer was not found"); while (1); }
	VCMHCC* hcc = &hcm->hcc_storage[hcc_index];

	char* mapping_address = 0;
	char* next_addr = 0;
	size_t requested_bytes = size;
	size_t requestedPages = (size_t)ceil((double)requested_bytes / page_size);


	if (!hcc->c_num) {
		mapping_address = hca;
		next_addr = hca;
	}
	else {
		VCMCC last_cc = hcc_getcc(hcc, hcc->c_num - 1);
		size_t last_guest_bytes = cc_guest_bytes(&last_cc);

		mapping_address = cc_next_mapping_address(&last_cc);
		next_addr = cc_next_address(&last_cc);

		if (requested_bytes > last_guest_bytes)
			requestedPages = (size_t)ceil((double)(requested_bytes - last_guest_bytes) / page_size); //https://stackoverflow.com/a/65784221/10772859
		else
			requestedPages = 0;
	}

	if (requestedPages) {
		hcc_fit_pfns(hcc, hcc->pfns_num + requestedPages);
		PULONG_PTR PFNs = hcc->pfns_storage + hcc->pfns_num;
		size_t allocatedPFNs = requestedPages;

		if (!AllocateUserPhysicalPages(GetCurrentProcess(), &allocatedPFNs, PFNs))
		{
			printf("hcm_insert_cc: Physical Memory Allocation Error , Probably Physical Memory Unavailable");
			while (1);
		}

		if (allocatedPFNs != requestedPages)
		{
			printf("hcm_insert_cc: Physical Memory Allocation Incomplete , Probably Physical Memory Unavailable");
			while (1);
		}

		if (requestedPages)
			if (!MapUserPhysicalPages(mapping_address, requestedPages, PFNs))
			{
				printf("hcm_insert_cc: MapUserPhysicalPages failed");
				while (1);
			}

		hcc->pfns_num += requestedPages;
	}

	hcc_fit_c(hcc, hcc->c_num + 1);

	hcc->pointer_storage[hcc->c_num] = next_addr;
	vcmref_insert(&hcc->ref_storage[hcc->c_num], userpointer);
	hcc->last_c_size = size;
	hcc->c_num++;

	*userpointer = next_addr;
};
void	hcm_insert_ref(VCMHCM* hcm, char* pointer, char** reference) {
	size_t hcc_index = hcm_search_c(hcm, pointer);
	if (hcc_index == -1) {
		printf("hcm_insert_ref: hyper container was not found");
		while (1);
	}
	VCMHCC* hcc = &hcm->hcc_storage[hcc_index];
	size_t c_index = hcc_search(hcc, pointer);
	if (c_index == -1) {
		printf("hcm_insert_ref: container was not found");
		while (1);
	}
	vcmref_insert(&hcc->ref_storage[c_index], reference);
}

void	hcm_remove_ref(VCMHCM* hcm, char* pointer, char** reference) {
	size_t hcc_index = hcm_search_c(hcm, pointer);
	if (hcc_index == -1) {
		printf("hcm_remove_ref: hyper container was not found");
		while (1);
	}
	VCMHCC* hcc = &hcm->hcc_storage[hcc_index];
	size_t c_index = hcc_search(hcc, pointer);
	if (c_index == -1) {
		printf("hcm_remove_ref: container was not found");
		while (1);
	}
	vcmref_remove(&hcc->ref_storage[c_index], reference);
}
void	hcm_remove_hc(VCMHCM* hcm, char* hypercontainer_address) {
	INT64 hc_index = hcm_search_hc(hcm, hypercontainer_address);
	VCMHCC hcc = hcm->hcc_storage[hc_index];
	if (hc_index == -1) {
		printf("hcm_free_hc: hyper container not found");
		while (1);
	}

	size_t frames_to_free = hcc.pfns_num;

	if (frames_to_free && FreeUserPhysicalPages(GetCurrentProcess(), &frames_to_free, hcc.pfns_storage) == FALSE) {
		printf("hcm_free_hc: couldn't free container frames");
		while (1);
	}
	if (frames_to_free != hcc.pfns_num) {
		printf("hcm_free_hc: couldn't free all container's frames");
		while (1);
	}
	if (!VirtualFree(hypercontainer_address, 0, MEM_RELEASE)) {
		printf("hcm_free_hc: couldn't free hypercontainer's virtual space");
		while (1);
	}
	HeapFree(GetProcessHeap(), 0, hcc.pfns_storage);
	HeapFree(GetProcessHeap(), 0, hcc.ref_storage);
	HeapFree(GetProcessHeap(), 0, hcc.pointer_storage);
	

	memmove(&hcm->hcc_storage[hc_index], &hcm->hcc_storage[hc_index + 1], sizeof(VCMHCC) * (hcm->hc_num - (hc_index + 1)));
	memmove(&hcm->p_storage[hc_index], &hcm->p_storage[hc_index + 1], sizeof(ULONG_PTR) * (hcm->hc_num - (hc_index + 1)));


	hcm->hc_num--;


}

clock_t hcm_realloc_c_old(VCMHCM* hcm, char* hc, char* user_pointer, size_t newSize) {
	clock_t start = 0, finish = 0;
		
	size_t PageSize = page_size;
	size_t hcc_i = hcm_search_hc(hcm, hc); if (hcc_i == -1) { printf("hcm_realloc_cc: hypercontainer was not found"); while (1); }
	VCMHCC* hcc = hcm->hcc_storage + hcc_i;
	VCMCC first_container = hcc_getcc(hcc, 0);
	VCMCC last_cc = hcc_getcc(hcc, hcc->c_num - 1);
	size_t concerned_container_i = hcc_search(hcc, user_pointer); if (concerned_container_i == -1) { printf("hcm_realloc_cc: container not found"); while (1); }
	VCMCC concerned_container = hcc_getcc(hcc, concerned_container_i);
	size_t startPFNsIdx = hcc_pfns_position(hcc, first_container.index, concerned_container.index);
	size_t priorFreeBytes = cc_prior_free_bytes(&concerned_container);
	size_t guestBytes = cc_guest_bytes(&concerned_container);
	INT64 requestedBytes = newSize - priorFreeBytes + guestBytes; if (requestedBytes < 0) requestedBytes = 0;
	size_t requestedPages = (size_t)ceil((double)requestedBytes / PageSize);
	size_t concerned_container_pages = cc_num_of_pages(&concerned_container);

	//Indexers
	size_t			containerPFNsIdx = startPFNsIdx;
	INT64			steps = 0;


	if (requestedPages < concerned_container_pages) {

		size_t steps = concerned_container_pages - requestedPages;

		//Copy guest
		void* src = cc_next_address(&concerned_container);
		void* dst = (char*)cc_next_address(&concerned_container) - steps * PageSize;
		if (!memmove(dst, src, cc_guest_bytes(&concerned_container))) {
			printf("hcm_realloc_cc: Coudln't copy guest bytes");
			while (1);
		}

		//Free frames
		size_t freeIdx = containerPFNsIdx + requestedPages;
		size_t pagesToFree = steps;
		if (!FreeUserPhysicalPages(GetCurrentProcess(), &pagesToFree, &hcc->pfns_storage[freeIdx])) {
			printf("hcm_realloc_cc: couldn't free remaining frames");
			while (1);
		}
		if (pagesToFree != steps) {
			printf("hcm_realloc_cc: couldn't free all remaining frames");
			while (1);
		}
		//Shift PFNs
		size_t	PFNsToMove = hcc->pfns_num - startPFNsIdx - concerned_container_pages;
		memmove(&hcc->pfns_storage[freeIdx], &hcc->pfns_storage[containerPFNsIdx + concerned_container_pages], PFNsToMove * sizeof(ULONG_PTR));

		//Remap
		size_t unmap_num = hcc->pfns_num - startPFNsIdx;
		size_t map_num = hcc->pfns_num - startPFNsIdx - steps;
		if (unmap_num && !MapUserPhysicalPages(cc_mapping_address(&concerned_container), unmap_num, NULL)) {
			printf("hcm_realloc_cc: couldn't unmap previous mappings");
			while (1);
		}
		if (map_num && !MapUserPhysicalPages(cc_mapping_address(&concerned_container), map_num, hcc->pfns_storage + startPFNsIdx)) {
			printf("hcm_realloc_cc: couldn't map comitted deallocation changes");
			while (1);
		}


		//Update container
		if (concerned_container.index == hcc->c_num - 1)
			hcc->last_c_size = requestedPages * PageSize + cc_prior_free_bytes(&concerned_container) - cc_guest_bytes(&concerned_container);
		
		start = clock(); /*=======================================================*/
		
		//Post-reallocation structs updates
		INT64 i;
		//#pragma omp parallel for
		for (i = concerned_container.index + 1; i <= (INT64)last_cc.index; i++)
		{
			hcc->pointer_storage[i] -= steps * PageSize;
			vcmref_shift(&hcc->ref_storage[i], -(INT64)steps * PageSize);
		}
		
		finish = clock(); /*=======================================================*/

		//Hyper Container update
		hcc->pfns_num -= steps;
	}
	else if (requestedPages > concerned_container_pages) {

		//Storage assertion
		hcc_fit_pfns(hcc, hcc->pfns_num + steps);


		//Shift PFNs
		void* PFNsSrc = &hcc->pfns_storage[startPFNsIdx + concerned_container_pages];
		void* PFNsDst = &hcc->pfns_storage[startPFNsIdx + requestedPages];
		size_t PFNsToMove = hcc->pfns_num - startPFNsIdx - concerned_container_pages;
		memmove(PFNsDst, PFNsSrc, PFNsToMove * sizeof(ULONG_PTR));
		size_t steps = requestedPages - concerned_container_pages;


		//Allocate needed PFNs
		size_t pagesToAlloc = steps;
		if (!AllocateUserPhysicalPages(GetCurrentProcess(), &pagesToAlloc, (PULONG_PTR)PFNsSrc)) {
			printf("hcm_realloc_cc: couldn't allocate additional required frames");
			while (1);
		}
		if (pagesToAlloc != steps) {
			printf("hcm_realloc_cc: couldn't allocate enough additional required frames");
			while (1);
		}

		start = clock(); /*=======================================================*/
		//Remap
		if (hcc->pfns_num != startPFNsIdx && !MapUserPhysicalPages(cc_mapping_address(&concerned_container), hcc->pfns_num - startPFNsIdx, NULL)) {
			printf("hcm_realloc_cc: couldn't unmap previous mappings");
			while (1);
		}
		if (!MapUserPhysicalPages(cc_mapping_address(&concerned_container), hcc->pfns_num - startPFNsIdx + steps, &hcc->pfns_storage[startPFNsIdx])) {
			printf("hcm_realloc_cc: couldn't map comitted reallocation changes");
			while (1);
		}
		finish = clock();/*=======================================================*/
		//Copy guest
		void* src = (char*)cc_next_address(&concerned_container);
		void* dst = (char*)cc_next_address(&concerned_container) + steps * PageSize;
		if (!memmove(dst, src, cc_guest_bytes(&concerned_container))) {
			printf("hcm_realloc_cc: couldn't couldn't copy guest bytes");
			while (1);
		}


		//Update container
		if (concerned_container.index == hcc->c_num - 1)
			hcc->last_c_size = (requestedPages * PageSize + cc_prior_free_bytes(&concerned_container) - cc_guest_bytes(&concerned_container));

		//Update post-reallocation containers

		INT64 i;
		//#pragma omp parallel for
		for (i = concerned_container.index + 1; i <= (INT64)last_cc.index; i++)
		{
			hcc->pointer_storage[i] += steps * PageSize;
			vcmref_shift(&hcc->ref_storage[i], steps * PageSize);
		}
		
		//Hyper Container update
		hcc->pfns_num += steps;

	}
	return  finish - start;
}

char* 	hcm_realloc_c(VCMHCM* hcm, char* user_pointer, size_t new_size){
	size_t hc_idx = hcm_search_c(hcm, user_pointer);
	if(hc_idx == -1){
		printf("hcm_realloc: couldn't find hyper container");
		while(1);
	}
	char* ptr = hcm_insert_c(hcm, hc_idx, new_size);
	VCMHCC* hcc = &hcm->hcc_storage[hc_idx];
	size_t c_idx = hcc_search_c(hcc, user_pointer);
	size_t c_size;
	if (c_idx == hcc->c_num - 1)
		c_size = hcc->last_c_size;
	else
		c_size = hcc->pointer_storage[c_idx + 1] - hcc->pointer_storage[c_idx];
	memcpy(ptr, user_pointer, c_size);

	return ptr;
}

void 	hcm_resize_c(VCMHCM* hcm, char* user_pointer, size_t newSize) {
	clock_t start = 0, finish = 0;
		
	size_t PageSize = page_size;
	size_t hcc_i = hcm_search_c(hcm, user_pointer); if (hcc_i == -1) { printf("hcm_resize_cc: hypercontainer was not found"); while (1); }
	VCMHCC* hcc = hcm->hcc_storage + hcc_i;
	VCMCC first_container = hcc_getcc(hcc, 0);
	VCMCC last_cc = hcc_getcc(hcc, hcc->c_num - 1);
	size_t concerned_container_i = hcc_search(hcc, user_pointer); if (concerned_container_i == -1) { printf("hcm_resize_cc: container not found"); while (1); }
	VCMCC concerned_container = hcc_getcc(hcc, concerned_container_i);
	size_t startPFNsIdx = hcc_pfns_position(hcc, first_container.index, concerned_container.index);
	size_t priorFreeBytes = cc_prior_free_bytes(&concerned_container);
	size_t guestBytes = cc_guest_bytes(&concerned_container);
	INT64 requestedBytes = newSize - priorFreeBytes + guestBytes; if (requestedBytes < 0) requestedBytes = 0;
	size_t requestedPages = (size_t)ceil((double)requestedBytes / PageSize);
	size_t concerned_container_pages = cc_num_of_pages(&concerned_container);

	//Indexers
	size_t			containerPFNsIdx = startPFNsIdx;
	INT64			steps = 0;


	if (requestedPages < concerned_container_pages) {

		size_t steps = concerned_container_pages - requestedPages;

		//Copy guest
		void* src = cc_next_address(&concerned_container);
		void* dst = (char*)cc_next_address(&concerned_container) - steps * PageSize;
		if (!memmove(dst, src, cc_guest_bytes(&concerned_container))) {
			printf("hcm_realloc_cc: Coudln't copy guest bytes");
			while (1);
		}

		//Free frames
		size_t freeIdx = containerPFNsIdx + requestedPages;
		size_t pagesToFree = steps;
		if (!FreeUserPhysicalPages(GetCurrentProcess(), &pagesToFree, &hcc->pfns_storage[freeIdx])) {
			printf("hcm_realloc_cc: couldn't free remaining frames");
			while (1);
		}
		if (pagesToFree != steps) {
			printf("hcm_realloc_cc: couldn't free all remaining frames");
			while (1);
		}
		//Shift PFNs
		size_t	PFNsToMove = hcc->pfns_num - startPFNsIdx - concerned_container_pages;
		memmove(&hcc->pfns_storage[freeIdx], &hcc->pfns_storage[containerPFNsIdx + concerned_container_pages], PFNsToMove * sizeof(ULONG_PTR));

		//Remap
		size_t unmap_num = hcc->pfns_num - startPFNsIdx;
		size_t map_num = hcc->pfns_num - startPFNsIdx - steps;
		if (unmap_num && !MapUserPhysicalPages(cc_mapping_address(&concerned_container), unmap_num, NULL)) {
			printf("hcm_realloc_cc: couldn't unmap previous mappings");
			while (1);
		}
		if (map_num && !MapUserPhysicalPages(cc_mapping_address(&concerned_container), map_num, hcc->pfns_storage + startPFNsIdx)) {
			printf("hcm_realloc_cc: couldn't map comitted deallocation changes");
			while (1);
		}


		//Update container
		if (concerned_container.index == hcc->c_num - 1)
			hcc->last_c_size = requestedPages * PageSize + cc_prior_free_bytes(&concerned_container) - cc_guest_bytes(&concerned_container);
		
		start = clock(); /*=======================================================*/
		
		//Post-reallocation structs updates
		INT64 i;
		//#pragma omp parallel for
		for (i = concerned_container.index + 1; i <= (INT64)last_cc.index; i++)
		{
			hcc->pointer_storage[i] -= steps * PageSize;
			vcmref_shift(&hcc->ref_storage[i], -(INT64)steps * PageSize);
		}
		
		finish = clock(); /*=======================================================*/

		//Hyper Container update
		hcc->pfns_num -= steps;
	}
	else if (requestedPages > concerned_container_pages) {

		//Storage assertion
		hcc_fit_pfns(hcc, hcc->pfns_num + steps);


		//Shift PFNs
		void* PFNsSrc = &hcc->pfns_storage[startPFNsIdx + concerned_container_pages];
		void* PFNsDst = &hcc->pfns_storage[startPFNsIdx + requestedPages];
		size_t PFNsToMove = hcc->pfns_num - startPFNsIdx - concerned_container_pages;
		memmove(PFNsDst, PFNsSrc, PFNsToMove * sizeof(ULONG_PTR));
		size_t steps = requestedPages - concerned_container_pages;


		//Allocate needed PFNs
		size_t pagesToAlloc = steps;
		if (!AllocateUserPhysicalPages(GetCurrentProcess(), &pagesToAlloc, (PULONG_PTR)PFNsSrc)) {
			printf("hcm_realloc_cc: couldn't allocate additional required frames");
			while (1);
		}
		if (pagesToAlloc != steps) {
			printf("hcm_realloc_cc: couldn't allocate enough additional required frames");
			while (1);
		}

		start = clock(); /*=======================================================*/
		//Remap
		if (hcc->pfns_num != startPFNsIdx && !MapUserPhysicalPages(cc_mapping_address(&concerned_container), hcc->pfns_num - startPFNsIdx, NULL)) {
			printf("hcm_realloc_cc: couldn't unmap previous mappings");
			while (1);
		}
		if (!MapUserPhysicalPages(cc_mapping_address(&concerned_container), hcc->pfns_num - startPFNsIdx + steps, &hcc->pfns_storage[startPFNsIdx])) {
			printf("hcm_realloc_cc: couldn't map comitted reallocation changes");
			while (1);
		}
		finish = clock();/*=======================================================*/
		//Copy guest
		void* src = (char*)cc_next_address(&concerned_container);
		void* dst = (char*)cc_next_address(&concerned_container) + steps * PageSize;
		if (!memmove(dst, src, cc_guest_bytes(&concerned_container))) {
			printf("hcm_realloc_cc: couldn't couldn't copy guest bytes");
			while (1);
		}


		//Update container
		if (concerned_container.index == hcc->c_num - 1)
			hcc->last_c_size = (requestedPages * PageSize + cc_prior_free_bytes(&concerned_container) - cc_guest_bytes(&concerned_container));

		//Update post-reallocation containers

		INT64 i;
		//#pragma omp parallel for
		for (i = concerned_container.index + 1; i <= (INT64)last_cc.index; i++)
		{
			hcc->pointer_storage[i] += steps * PageSize;
			vcmref_shift(&hcc->ref_storage[i], steps * PageSize);
		}
		
		//Hyper Container update
		hcc->pfns_num += steps;

	}

}
void	hcm_multiresize_c(VCMHCM* hcm, char* user_pointer, size_t* sizes_array, size_t array_size) {
	size_t hcc_i = hcm_search_c(hcm, user_pointer); if (hcc_i == -1) { printf("hcm_multirealloc_cc: hypercontainer was not found"); while (1); }
	VCMHCC* hcc = hcm->hcc_storage + hcc_i;
	size_t requested_pages = 0;
	ULONG_PTR* sparePFNs = (ULONG_PTR*)HeapAlloc(GetProcessHeap(), 0, hcc->pfns_num * sizeof(ULONG_PTR)); // To store spare PFNs
	ULONG_PTR* tempPFNs = (ULONG_PTR*)HeapAlloc(GetProcessHeap(), 0, hcc->pfns_num * sizeof(ULONG_PTR)); // Temporary helper PFNs array

	// Start conditions
	size_t start_container_i = hcc_search(hcc, user_pointer); if (start_container_i == -1) { printf("hcm_multirealloc_cc: container not found"); while (1); }
	VCMCC start_container = hcc_getcc(hcc, start_container_i);
	VCMCC last_container = hcc_getcc(hcc, hcc->c_num - 1);
	VCMCC first_container = hcc_getcc(hcc, 0);


	size_t startPFNsIdx = hcc_pfns_position(hcc, first_container.index, start_container.index);
	size_t startTempPFNsIdx = 0;
	size_t startSteps = 0;
	size_t startNumberOfPFNs = hcc->pfns_num;

	// Indexers
	size_t it_index = start_container.index;
	ULONG_PTR PFNsIdx = startPFNsIdx;
	size_t tempPFNsIdx = startTempPFNsIdx;
	size_t steps = startSteps;

	// Globals to be calculated later
	size_t numberOfPostReallocPages = 0;

	for (size_t i = 0; i < array_size; i++) {
		INT64 new_size = sizes_array[i];
		VCMCC it_cc = hcc_getcc(hcc, it_index);
		size_t container_pages = cc_num_of_pages(&it_cc);

		if ((new_size == it_cc.size) || new_size == -1) {

			memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, container_pages * sizeof(ULONG_PTR));

			PFNsIdx += container_pages;
			tempPFNsIdx += container_pages;

			hcc->pointer_storage[it_index] -= steps * page_size;
			vcmref_shift(&hcc->ref_storage[it_index], -(INT64)steps * page_size);

			it_index += 1;

			continue;
		}

		size_t	itStructPriorFreeBytes = cc_prior_free_bytes(&it_cc);
		size_t	itStructGuestBytes = cc_guest_bytes(&it_cc);

		INT64	requestedBytes = new_size - itStructPriorFreeBytes + itStructGuestBytes; if (requestedBytes < 0) requestedBytes = 0;
		size_t	requestedNumberOfPages = (size_t)ceil((double)requestedBytes / page_size);

		if (requestedNumberOfPages == container_pages) {
			memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, container_pages * sizeof(ULONG_PTR));

			PFNsIdx += container_pages;
			tempPFNsIdx += container_pages;

			hcc->pointer_storage[it_index] -= steps * page_size;
			vcmref_shift(&hcc->ref_storage[it_index], -(INT64)steps* page_size);

			it_index += 1;

			continue;
		}
		if (requestedNumberOfPages > container_pages) {
			requested_pages += requestedNumberOfPages - container_pages;

			memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, container_pages * sizeof(ULONG_PTR));

			PFNsIdx += container_pages;
			tempPFNsIdx += container_pages;

			hcc->pointer_storage[it_index] -= steps * page_size;
			vcmref_shift(&hcc->ref_storage[it_index], -(INT64)steps * page_size);

			it_index += 1;

			continue;
		}
		if (requestedNumberOfPages < container_pages) {

			PVOID itStructNextMappingAddress = cc_next_mapping_address(&it_cc);
			size_t itStructGuestBytes = cc_guest_bytes(&it_cc);

			size_t itSteps = container_pages - requestedNumberOfPages;
			PVOID source = (char*)itStructNextMappingAddress - itStructGuestBytes;
			PVOID destination = (char*)source - (itSteps)*page_size;

			memmove(destination, source, itStructGuestBytes);
			memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, requestedNumberOfPages * sizeof(ULONG_PTR));
			memmove(sparePFNs + steps, hcc->pfns_storage + PFNsIdx + requestedNumberOfPages, itSteps * sizeof(ULONG_PTR));

			PFNsIdx += container_pages;
			tempPFNsIdx += requestedNumberOfPages;

			if (it_cc.index == hcc->c_num - 1) hcc->last_c_size += (requestedNumberOfPages * page_size + itStructPriorFreeBytes - itStructGuestBytes);
			
			hcc->pointer_storage[it_index] -= steps * page_size;
			vcmref_shift(&hcc->ref_storage[it_index], -(INT64)steps * page_size);

			steps += itSteps;
			hcc->pfns_num -= itSteps;
			sizes_array[i] = -1;

			it_index += 1;
		}
	}	
	for (size_t i = it_index; i <= last_container.index; i++) {
		hcc->pointer_storage[i] -= steps * page_size;
		vcmref_shift(&hcc->ref_storage[i], -(INT64)steps * page_size);
	}
	
	numberOfPostReallocPages = startNumberOfPFNs - PFNsIdx;
	size_t numberofPagesToCommit = tempPFNsIdx + numberOfPostReallocPages;
	memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, numberOfPostReallocPages * sizeof(ULONG_PTR));
	memmove(hcc->pfns_storage + startPFNsIdx, tempPFNs, (numberofPagesToCommit) * sizeof(ULONG_PTR));

	if (startNumberOfPFNs != startPFNsIdx && !MapUserPhysicalPages(cc_mapping_address(&start_container), startNumberOfPFNs - startPFNsIdx, NULL)) {
		printf("hcm_multirealloc_cc: couldn't unmap previous mappings");
		while (1);
	}

	if (numberofPagesToCommit && !MapUserPhysicalPages(cc_mapping_address(&start_container), numberofPagesToCommit, hcc->pfns_storage + startPFNsIdx)) {
		printf("hcm_multirealloc_cc: couldn't map comitted deallocation changes");
		while (1);
	}

	if (!HeapFree(GetProcessHeap(), 0, tempPFNs)) {
		printf("hcm_multirealloc_cc: couldn't free the temporary PFNs array");
		while (1);
	}

	INT64 remainingPages = steps - requested_pages;

	if (remainingPages > 0) {
		size_t pagesToFree = remainingPages;
		if (FreeUserPhysicalPages(GetCurrentProcess(), &pagesToFree, sparePFNs + requested_pages) == FALSE)
			printf("hcm_multirealloc_cc: couldn't free remaining frames");

		if (pagesToFree != remainingPages)
			printf("hcm_multirealloc_cc: couldn't free all remaining frames");
	}

	if (requested_pages > 0) {

		if (remainingPages < 0) {

			sparePFNs = (ULONG_PTR*)HeapReAlloc(GetProcessHeap(), 0, sparePFNs, (steps + -remainingPages) * sizeof(ULONG_PTR));

			size_t pagesToAlloc = -remainingPages;
			if (AllocateUserPhysicalPages(GetCurrentProcess(), &pagesToAlloc, sparePFNs + steps) == FALSE)
				printf("vcrealloc: couldn't allocate additional required frames");

			if (pagesToAlloc + remainingPages != 0)
				printf("vcrealloc: couldn't allocate enough additional required frames");
		}


		tempPFNs = (ULONG_PTR*)HeapAlloc(GetProcessHeap(), 0, (hcc->pfns_num + requested_pages) * sizeof(ULONG_PTR));
		PVOID copy_buffer = VirtualAlloc(NULL, page_size, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);


		// Indexers
		it_index = start_container.index;
		PFNsIdx = startPFNsIdx;
		tempPFNsIdx = startTempPFNsIdx;
		steps = startSteps;

		hcc_fit_pfns(hcc, hcc->pfns_num + requested_pages);
		//Successive allocation loop
		for (size_t i = 0; i < array_size; i++) {
			INT64	new_size = sizes_array[i];
			VCMCC it_cc = hcc_getcc(hcc, it_index);
			size_t	container_pages_number = cc_num_of_pages(&it_cc);

			if (new_size == it_cc.size || new_size == -1) {

				memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, container_pages_number * sizeof(ULONG_PTR));

				PFNsIdx += container_pages_number;
				tempPFNsIdx += container_pages_number;

				hcc->pointer_storage[it_index] -= steps * page_size;
				vcmref_shift(&hcc->ref_storage[it_index], steps * page_size);

				it_index += 1;

				continue;
			}

			size_t	itStructPriorFreeBytes = cc_prior_free_bytes(&it_cc);
			size_t	itStructGuestBytes = cc_guest_bytes(&it_cc);

			INT64	requestedBytes = new_size - itStructPriorFreeBytes + itStructGuestBytes; if (requestedBytes < 0) requestedBytes = 0;
			size_t	requestedNumberOfPages = (size_t)ceil((double)requestedBytes / page_size);

			if (requestedNumberOfPages == container_pages_number) {
				memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, container_pages_number * sizeof(ULONG_PTR));

				PFNsIdx += container_pages_number;
				tempPFNsIdx += container_pages_number;

				hcc->pointer_storage[it_index] -= steps * page_size;
				vcmref_shift(&hcc->ref_storage[it_index], steps * page_size);

				it_index += 1;

				continue;
			}
			if (requestedNumberOfPages > container_pages_number) {

				PVOID	itStructNextMappingAddress = cc_next_mapping_address(&it_cc);
				size_t	itStructLastPageOffset = cc_last_page_offset(&it_cc);


				size_t itSteps = requestedNumberOfPages - container_pages_number;
				PVOID source = (char*)itStructNextMappingAddress - itStructGuestBytes;
				PVOID destination = (char*)copy_buffer + itStructLastPageOffset;

				if (!MapUserPhysicalPages(copy_buffer, 1, sparePFNs + requested_pages - itSteps))
					printf("vcrealloc: failed to map guest destination");

				memmove(destination, source, itStructGuestBytes);
				memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, container_pages_number * sizeof(ULONG_PTR));
				memmove(tempPFNs + tempPFNsIdx + container_pages_number, sparePFNs + requested_pages - itSteps, itSteps * sizeof(ULONG_PTR));

				if (!MapUserPhysicalPages(copy_buffer, 1, NULL))
					printf("vcrealloc: failed to ummap guest destination");

				PFNsIdx += container_pages_number;
				tempPFNsIdx += requestedNumberOfPages;
				
				if(it_cc.index == hcc->c_num-1)
					hcc->last_c_size = (requestedNumberOfPages * page_size + itStructPriorFreeBytes - itStructGuestBytes);
				
				hcc->pointer_storage[it_index] += steps * page_size;
				vcmref_shift(&hcc->ref_storage[it_index], steps* page_size);

				requested_pages -= itSteps;
				steps += itSteps;
				hcc->pfns_num += itSteps;
				it_index++;

			}
		}
		
		for (size_t i = it_index; i <= last_container.index; i++) {
			hcc->pointer_storage[i] += steps * page_size;
			vcmref_shift(&hcc->ref_storage[i], steps* page_size);
		}



		size_t numberofPagesToCommit = tempPFNsIdx + numberOfPostReallocPages;

		//Copy post reallocation pages
		memmove(tempPFNs + tempPFNsIdx, hcc->pfns_storage + PFNsIdx, numberOfPostReallocPages * sizeof(ULONG_PTR));

		//Commit changes
		memmove(hcc->pfns_storage + startPFNsIdx, tempPFNs, (numberofPagesToCommit) * sizeof(ULONG_PTR));


		if (startNumberOfPFNs != startPFNsIdx && !MapUserPhysicalPages(cc_mapping_address(&start_container), startNumberOfPFNs - startPFNsIdx, NULL))
			printf("vcrealloc: couldn't unmap old frames upon allocation");

		if (numberofPagesToCommit && !MapUserPhysicalPages(cc_mapping_address(&start_container), numberofPagesToCommit, hcc->pfns_storage + startPFNsIdx))
			printf("vcrealloc: couldn't map comitted allocation changes");

		if (!HeapFree(GetProcessHeap(), 0, tempPFNs))
			printf("vcrealloc: couldn't free the temporary PFNs array");

		if (!VirtualFree(copy_buffer, 0, MEM_RELEASE))
			printf("vcrealloc: failed to release virtuall buffer");

	}

	if (!HeapFree(GetProcessHeap(), 0, sparePFNs)) {
		printf("hcm_multirealloc_cc: couldn't free the spare PFNs array");
		while (1);
	}

}

void	hcm_clear(VCMHCM* hcm) {

	for (size_t i = 0; i < hcm->hc_num; i++)
	{
		VCMHCC* hcc = &hcm->hcc_storage[i];
		size_t frames_to_free = hcc->pfns_num;
		if (frames_to_free && FreeUserPhysicalPages(GetCurrentProcess(), &frames_to_free, hcc->pfns_storage) == FALSE) {
			printf("hcm_clear: couldn't free container frames");
			while (1);
		}

		if (frames_to_free != hcc->pfns_num) {
			printf("hcm_clear: couldn't free all container's frames");
			while (1);
		}

		if (!VirtualFree(hcm->p_storage[i], 0, MEM_RELEASE)) {
			printf("hcm_clear: couldn't free hypercontainer's virtual space");
			while (1);
		}

		HeapFree(GetProcessHeap(), 0, hcc->pfns_storage);
		HeapFree(GetProcessHeap(), 0, hcc->ref_storage);
		HeapFree(GetProcessHeap(), 0, hcc->pointer_storage);
	}

	HeapFree(GetProcessHeap(), 0, hcm->hcc_storage);
	HeapFree(GetProcessHeap(), 0, hcm->p_storage);
	hcm->hc_max = 0;
	hcm->hc_num = 0;
	hcm->last_hc_size = 0;

}

void 	hcm_print(VCMHCM* hcm) {
	printf("Hypercontainer manager:\n");
	printf("hc_num: %zd\n", hcm->hc_num);
	printf("hc_max: %zd\n", hcm->hc_max);
	printf("last_hc_size: %zd\n", hcm->last_hc_size);
	printf("\nHypercontainers:\n");

	printf("%-5s %-16s %-14s %-17s %-6s %-9s %-22s\n", "HC #", "Address", "Containers", "Containers max", "PFNs", "PFNs max", "Last container size");
	printf("--------------------------------------------------------------------------------------------\n");

	int total_containers = 0;
	int total_containers_max = 0;
	int total_pfns = 0;
	int total_pfns_max = 0;
	int total_last_c_size = 0;

	for(size_t i = 0; i < hcm->hc_num; i++){
		VCMHCC* hcc = &hcm->hcc_storage[i];
		printf("%-5zd %-16p %-14zd %-17zd %-6zd %-9zd %-22zd\n", i+1, hcm->p_storage[i], hcc->c_num, hcc->c_max, hcc->pfns_num, hcc->pfns_max, hcc->last_c_size);
		total_containers += hcc->c_num;
		total_containers_max += hcc->c_max;
		total_pfns += hcc->pfns_num;
		total_pfns_max += hcc->pfns_max;
		total_last_c_size += hcc->last_c_size;
	}
	printf("--------------------------------------------------------------------------------------------\n");
	printf("%-5s %-16s %-14d %-17d %-6d %-9d %-22d\n", "Total", "", total_containers, total_containers_max, total_pfns, total_pfns_max, total_last_c_size);
	printf("--------------------------------------------------------------------------------------------\n");

}

VCMMEM  hcm_get_memuseage(VCMHCM* hcm){
	VCMMEM vcmmem = {0};
	vcmmem.vcmdata += hcm->hc_max*sizeof(VCMHCC);
	for (size_t hc = 0; hc < hcm->hc_num; hc++)
	{
		VCMHCC* hcc = &hcm->hcc_storage[hc];
		vcmmem.vcmdata += hcc->c_max * (sizeof(VCMREF) + sizeof(char*));
		vcmmem.vcmdata += hcc->pfns_max * sizeof(size_t);
		vcmmem.userdata += hcc->pfns_num*page_size;
	}

	vcmmem.total = vcmmem.userdata + vcmmem.vcmdata;

	return vcmmem;
	
}

/// The following apis abstract the hyper container manager
VCMHCM default_hcm; //default hyper container manager

char* vc_hcalloc(size_t size)
{
	hcm_insert_hc(&default_hcm, size);
	return default_hcm.p_storage[default_hcm.hc_num - 1];
};
char* vc_malloc(char* hc, size_t size) {
	size_t hc_i = hcm_search_c(&default_hcm, hc);
	return hcm_insert_c(&default_hcm, hc_i, size);
}
void* vc_malloc_r(char* hc_addr, char** user_pointer, size_t size) {
	hcm_insert_c_r(&default_hcm, hc_addr, user_pointer, size);
	return (void*)*user_pointer;
}
void  vc_addref(char* p, char** r) {
	hcm_insert_ref(&default_hcm, p, r);
}
void  vc_removeref(char* p, char** r) {
	hcm_remove_ref(&default_hcm, p, r);
}
void  vc_resize(char* user_ptr, size_t new_size) {
	hcm_resize_c(&default_hcm, user_ptr, new_size);
}
void  vc_multiresize(char* user_ptr, size_t* sizes_array, size_t array_size) {
	hcm_multiresize_c(&default_hcm, user_ptr, sizes_array, array_size);
}
char* vc_realloc(char* user_ptr, size_t new_size) {
	return hcm_realloc_c(&default_hcm, user_ptr, new_size);
}
void  vc_hcfree(char* hcc) {
	hcm_remove_hc(&default_hcm, hcc);
}

/* 
 * The following apis abstract the hyper container manager and the hyper container context
 * it is used to make VCMAlloc compatible with Malloc.
*/

VCMHCA default_hca;
VCMHCM hca_hcm;
void hca_init(size_t hc_size, size_t c_per_hc, size_t hc_num) {
	//if (!InitializeCriticalSectionAndSpinCount(&CriticalSection, 0x00000400)) { printf("CS init failed\n"); while (1); }
	//printf("VCMAlloc Hyper Container Automanager\n");
	default_hca.used_hcm = &hca_hcm; if (!default_hca.used_hcm->hc_max) hcm_init_fit(default_hca.used_hcm, hc_num);
	default_hca.hc_size = hc_size;
	default_hca.c_per_hc = c_per_hc;
}
char* vca_malloc(size_t size) {
	
	VCMHCM* hcm = default_hca.used_hcm;
	bool create_new_hc = false;
	
	if (!hcm->hc_num)
		create_new_hc = true;
	else{
		VCMHCC* hcc = &hcm->hcc_storage[hcm->hc_num - 1];
		char* hcc_addr = hcm->p_storage[hcm->hc_num - 1];
		size_t c_num = hcc->c_num;
		if(c_num){
			size_t used_size = (hcc->pointer_storage[hcc->c_num - 1] + hcc->last_c_size) - hcc_addr;
			if (used_size + size > hcm->last_hc_size)
				create_new_hc = true;
		}	
	}

	if (create_new_hc) {
		//printf("new hc\n");
		hcm_insert_hc_fit(hcm, default_hca.hc_size, (size_t)ceil((long double)default_hca.hc_size / page_size), default_hca.c_per_hc);
	}

	return hcm_insert_c(hcm, hcm->hc_num - 1, size);
}
char* vca_calloc(size_t n, size_t size) {
	char* ptr = vca_malloc(n * size);
	memset(ptr, 0, n * size);
	return ptr;
}
void vcar_malloc(char** user_pointer, size_t size) {
	VCMHCM* hcm = default_hca.used_hcm;
	bool create_new_hc = false;
	
	if (!hcm->hc_num)
		create_new_hc = true;
	else{
		VCMHCC* hcc = &hcm->hcc_storage[hcm->hc_num - 1];
		char* hcc_addr = hcm->p_storage[hcm->hc_num - 1];
		size_t c_num = hcc->c_num;
		if(c_num){
			size_t used_size = (hcc->pointer_storage[hcc->c_num - 1] + hcc->last_c_size) - hcc_addr;
			if (used_size + size > hcm->last_hc_size)
				create_new_hc = true;
		}	
	}

	if (create_new_hc) {
		//printf("new hc\n");
		hcm_insert_hc_fit(hcm, default_hca.hc_size, (size_t)ceil((long double)default_hca.hc_size / page_size), default_hca.c_per_hc);
	}

	hcm_insert_c_r(hcm, hcm->p_storage[hcm->hc_num - 1], user_pointer, size);
}
char* vca_realloc(char* user_pointer, size_t new_size){
	return hcm_realloc_c(default_hca.used_hcm, user_pointer, new_size);
}
void vca_resize(char* user_ptr, size_t new_size) {
	hcm_resize_c(&hca_hcm, user_ptr, new_size);
}
void vca_multiresize(char* user_ptr, size_t* sizes_array, size_t array_size) {
	hcm_multiresize_c(&hca_hcm, user_ptr, sizes_array, array_size);
}

/// The following apis seperate eaach thread data into a different hypercontainer (very early experimental stage)
VCMTHC vcmthc = {0};
void vcm_init_th(size_t hc_size_th, size_t c_num_th) {
	if (!vcmthc.hcm.hc_max) {
		hcm_init(&vcmthc.hcm);
		vcmthc.thid = (unsigned long*)HeapAlloc(GetProcessHeap(), 0, vcmthc.hcm.hc_max*sizeof(unsigned long));
		memset(vcmthc.thid, 0, page_size);
	}
	vcmthc.hc_size_th = hc_size_th;
	vcmthc.c_num_th = c_num_th;
}
size_t vc_search_th(unsigned long thid) {
	size_t max = vcmthc.hcm.hc_num;
	for (size_t i = 0; i < max; i++)
		if (vcmthc.thid[i] == thid)
			return i;
	return -1;
}
void vc_malloc_th(char** user_pointer, size_t size) {
	VCMHCM* used_hcm = &vcmthc.hcm;
	unsigned long thid = GetCurrentThreadId();
	size_t hc_i = vc_search_th(thid);
	printf("\nalloc: %zd: %d'\n", hc_i, thid);
	if (hc_i == -1) {
		
		// vcmthc fit
		if (used_hcm->hc_num == used_hcm->hc_max) {
			size_t hcc_per_page = (INT64)(page_size / sizeof(VCMHCC));
			used_hcm->hc_max += hcc_per_page;

			used_hcm->hcc_storage = (VCMHCC*)HeapReAlloc(GetProcessHeap(), 0, used_hcm->hcc_storage, (used_hcm->hc_max) * sizeof(VCMHCC));
			used_hcm->p_storage = (char**)HeapReAlloc(GetProcessHeap(), 0, used_hcm->p_storage, (used_hcm->hc_max) * sizeof(PVOID));
			(& vcmthc)->thid = (unsigned long*)HeapReAlloc(GetProcessHeap(), 0, vcmthc.thid, (used_hcm->hc_max) * sizeof(unsigned long));

			if (!used_hcm->hcc_storage || !used_hcm->p_storage || !vcmthc.thid) {
				printf("vc_malloc_th :: HeapReAlloc failed");
				while (1);
			}
		}

		// vcmthc insert
		used_hcm->p_storage[used_hcm->hc_num] = hcm_virtualalloc(used_hcm, &vcmthc.hc_size_th);
		used_hcm->last_hc_size = vcmthc.hc_size_th;
		vcmthc.thid[used_hcm->hc_num] = thid;
		VCMHCC* hcc = &used_hcm->hcc_storage[used_hcm->hc_num];
		hcc_init(hcc);
		hcc_fit_c(hcc, vcmthc.c_num_th);
		hcc_fit_pfns(hcc, (size_t)ceil((long double)vcmthc.hc_size_th/ page_size));
		hc_i = used_hcm->hc_num++;
	}

	hcm_insert_c_r(used_hcm, used_hcm->p_storage[hc_i], user_pointer, size);
}
void vc_realloc_th(char* user_pointer, size_t new_size) {
	VCMHCM* used_hcm = &vcmthc.hcm;
	unsigned long thid = GetCurrentThreadId();
	size_t hc_i = vc_search_th(thid);
	
	printf("\nrealloc: %zu: %i'\n", hc_i, thid);
	
	if (hc_i == -1) {
		printf("vc_realloc_th: thread was not found"); while (1);
	}
	
	VCMHCC* hcc = &used_hcm->hcc_storage[hc_i];
	hcm_realloc_c(used_hcm, user_pointer, new_size);
}
