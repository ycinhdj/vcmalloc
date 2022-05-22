#pragma once
#include <windows.h>
#include <string>
#include <math.h>



namespace vcma{

	SYSTEM_INFO	system_info;
	ULONG_PTR page_size;

	template<typename DataType> inline INT64 Distance(DataType* Ptr, INT64 Index, char* Elem);

	struct Search
	{

		static size_t MSB(size_t n) {
			size_t msb = 1;
			while (msb <= n)
				msb <<= 1;
			msb >>= 1;
			return msb;
		}


		template <typename Type>
		static size_t binarySearch(size_t max, Type* pointer, char* elem) {

			max = max - 1;
			size_t msb = MSB(max);
			size_t index = msb;

			while (Distance(pointer, index, elem) < 0){
				msb = MSB(max - index);
				if (msb == 0)
					return -1;
				index += msb;
			}


			while (true) {
				INT64 diff = Distance(pointer, index, elem);
				if (diff == 0) return index;

				if (diff < 0)
					index += msb;
				else
					index -= msb;

				if (msb == 0)
					return -1;

				msb /= 2;

			}



			return index;

		}

	};

	struct ContainerContext
	{
		ContainerContext(PVOID& Ptr, SIZE_T SizeOfType, SIZE_T Nbr_Elem) :reference_(Ptr), type_size_(SizeOfType), elements_num_(Nbr_Elem) {};

		SIZE_T				offset() {
			return (ULONG_PTR)reference_ & (page_size - 1);
		}
		PVOID				mappingAddress() {
			if (offset())
				return (char*)((ULONG_PTR)reference_ & ~(page_size - 1)) + page_size;
			else
				return  (char*)reference_;
		}
		PVOID				nextAddress() {
			return (PVOID)((ULONG_PTR)reference_ + type_size_ * elements_num_);
		}
		PVOID				nextMappingAddress() {
			SIZE_T lastPageOffset = (ULONG_PTR)nextAddress() & (page_size - 1);
			if (lastPageOffset)
				return (PVOID)(((ULONG_PTR)nextAddress() & ~(page_size - 1)) + page_size);
			else
				return nextAddress();
		}
		SIZE_T				lastPageOffset() {
			return (SIZE_T)nextAddress()& (page_size - 1);
		}
		SIZE_T				nbrOfPages() {
			return ((char*)nextMappingAddress() - (char*)mappingAddress())/page_size;
		}
		SIZE_T				guestBytes() {
			return (ULONG_PTR)nextMappingAddress() - (ULONG_PTR)nextAddress();
		}
		SIZE_T				priorFreeBytes() {
			return (char*)mappingAddress() - (char*) reference_;
		}
		
		PVOID&				reference_;
		SIZE_T				type_size_;
		SIZE_T				elements_num_;


	};

	struct HyperContainerContext {

		HyperContainerContext(SIZE_T size)
		{
			pfns_num_				= 0;
			containers_num_				= 0;
			hypercontainer_size_			= size;
			pfns_max_			= (INT64(page_size / sizeof(ULONG_PTR)));
			containers_max_			= (INT64(page_size / sizeof(ContainerContext)));
			pfns_storage_					= (ULONG_PTR*)HeapAlloc(GetProcessHeap(), 0, pfns_max_ * sizeof(ULONG_PTR));
			containercontext_storage_	= (ContainerContext*)malloc(containers_max_ * sizeof(ContainerContext));

		};

		ContainerContext*	end()
		{
			return containercontext_storage_ + containers_num_ - 1;
		};
		ContainerContext*	first()
		{
			return containercontext_storage_;
		};
		ContainerContext&	insert(PVOID& User_Ptr, SIZE_T SizeOfType, SIZE_T Nbr_Elem)
		{
			AssertContainerContextStorage(1);
			containers_num_++;
			new (containercontext_storage_ + containers_num_ - 1) ContainerContext(User_Ptr, SizeOfType, Nbr_Elem);
			return containercontext_storage_[containers_num_ - 1];
		};
		void				AssertPfnsStorage(SIZE_T additional_pfns_num) {		
			
			if (pfns_max_ < (pfns_num_ + additional_pfns_num)) {

				SIZE_T new_pfns_max = pfns_max_;

				while (new_pfns_max < (pfns_num_ + additional_pfns_num))
					new_pfns_max *= 2;

				pfns_storage_ = (ULONG_PTR*)HeapReAlloc(GetProcessHeap(), 0, pfns_storage_, new_pfns_max * sizeof(ULONG_PTR));
				if (!pfns_storage_)
					throw std::string("vcmalloc : Couldn't allocate space for Container's aPFNs array");

				pfns_max_ = new_pfns_max;

			}
		}
		void				AssertContainerContextStorage(SIZE_T additional_cc_num){
			if (containers_max_ < containers_num_ + additional_cc_num) {
				
				size_t container_per_page = int(page_size / sizeof(ContainerContext));
				SIZE_T newMax = containers_max_;
				
				while (newMax < containers_num_ + additional_cc_num)
					newMax += container_per_page;
				
				containercontext_storage_ = ((ContainerContext*)realloc(containercontext_storage_, (newMax) * sizeof(ContainerContext)));
				if (!containercontext_storage_)
					throw std::string("vcmalloc :: Unable to extend ContainerContextStorage");
				
				containers_max_ = newMax;
			}
		}
		SIZE_T				getPFNsPosition(ContainerContext* start, ContainerContext* end) {
			ULONG_PTR PFNsPosition = 0;

			for (ContainerContext* structContext = start; structContext != end; structContext++)
				PFNsPosition += structContext->nbrOfPages();

			return PFNsPosition;
		}
		template<typename DataType>ContainerContext* operator [] (DataType& User_Ptr)
		{
			INT64 Position = Search::binarySearch(containers_num_, containercontext_storage_, (char*&)User_Ptr);
			if (Position == -1)
			{
				return  nullptr;
			};
			return containercontext_storage_ + Position;
		};

		//Storage
		ContainerContext*	containercontext_storage_;	// Pointer to StructContext instances
		SIZE_T*				pfns_storage_;				// Allocated Page Frame Numbers

		//Information
		SIZE_T				containers_num_;			// Number of structs this hypercontainer_address's information system is currently hosting
		SIZE_T				containers_max_;			// Max number of structs this hypercontainer_address's information system is currently capable of hosting
		SIZE_T				pfns_num_;					// Number of pages this hypercontainer_address's information system is currently hosting
		SIZE_T				pfns_max_;					// Max number of pages this hypercontainer_address's information system is capable of hosting
		SIZE_T				hypercontainer_size_;

	};

	//https://blog.mbedded.ninja/programming/languages/c-plus-plus/magic-statics/
	struct HyperContainerManager final
	{
	
	public:
		static HyperContainerManager& getInstance() {
			static HyperContainerManager hcm;
			return (HyperContainerManager&)hcm;
		}

		template <typename DataType>HyperContainerContext* operator()(DataType& Pointer)
		{

			INT64 Position = Search::binarySearch(hypercontainers_num_, (char**&)pointers_storage_, (char*&)Pointer);
			if (Position == -1)
				return  nullptr;
			return hyper_containers_storage_ + Position;
		};
		template <typename DataType>HyperContainerContext& operator[](DataType& Pointer)
		{

			INT64 Position = Search::binarySearch(hypercontainers_num_, (char**&)pointers_storage_, (char*&)Pointer);
			if (Position == -1)
			{
				//return nullptr;
			};
			return hyper_containers_storage_[Position];
		};
		HyperContainerContext&	end()
		{
			//StructContext& Mypage = Page_Storage[0].StructContext_Storage[0];
			return hyper_containers_storage_[hypercontainers_num_ - 1];
		};
		HyperContainerContext&	insert(PVOID& NewPointer, SIZE_T size)
		{
			if (hypercontainers_num_+1 >= hypercontainers_max_)
			{
				size_t NbrElem = int(page_size / sizeof(HyperContainerContext));
				hyper_containers_storage_ = (HyperContainerContext*)realloc(hyper_containers_storage_, (hypercontainers_max_ + NbrElem) * sizeof(HyperContainerContext));
				pointers_storage_ = (PVOID*)realloc(pointers_storage_, (hypercontainers_max_ + NbrElem) * sizeof(PVOID));
				if (hyper_containers_storage_ && pointers_storage_)
				{
					hypercontainers_max_ = hypercontainers_max_ + NbrElem;
				}
				else
				{
					throw std::string("CL_MALLOC :: _Struct_Manager , Probably Physical Memory Unavailable");
				};

			};
			hypercontainers_num_++;
			
			new (&hyper_containers_storage_[hypercontainers_num_ - 1]) HyperContainerContext(size);
			pointers_storage_[hypercontainers_num_ - 1] = NewPointer;

			default_container_ = hyper_containers_storage_ + hypercontainers_num_ - 1;

			return hyper_containers_storage_[hypercontainers_num_ - 1];
		};

		HyperContainerContext*	default_container_;
		HyperContainerContext*	hyper_containers_storage_;
		PVOID*				pointers_storage_;
		ULONG_PTR			hypercontainers_num_;
		ULONG_PTR			hypercontainers_max_;

	private:
		HyperContainerManager()
		{
			TOKEN_PRIVILEGES token_priviledges;
			token_priviledges.PrivilegeCount = 1;
			token_priviledges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			HANDLE Token;
			
			if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &Token))
				if (LookupPrivilegeValue(NULL, SE_LOCK_MEMORY_NAME, &(token_priviledges.Privileges[0].Luid)))
					AdjustTokenPrivileges(Token, FALSE, &token_priviledges, 0, NULL, NULL);
			
			if (GetLastError() != ERROR_SUCCESS)
				throw std::string("AdjustTokenPrivileges Error");
			
			CloseHandle(Token);

			GetSystemInfo(&system_info);
			page_size = system_info.dwPageSize;

			hypercontainers_max_			= int(page_size / sizeof(HyperContainerContext));
			hyper_containers_storage_				= (HyperContainerContext*)malloc(hypercontainers_max_ * sizeof(HyperContainerContext));
			pointers_storage_				= (PVOID*)malloc(hypercontainers_max_ * sizeof(PVOID));
			hypercontainers_num_			= 0;
			default_container_				= nullptr;
		};
		HyperContainerManager(const HyperContainerManager&) = delete;
		HyperContainerManager(HyperContainerManager&&) = delete;
		HyperContainerManager& operator=(const HyperContainerManager&) = delete;
		HyperContainerManager& operator=(HyperContainerManager&&) = delete;

	};

	template<typename DataType> inline INT64	Distance(DataType* Ptr, INT64 Index, char* Elem)
	{
		return (char*)Ptr[Index] - Elem;
	};
	template<> inline INT64						Distance<ContainerContext>(ContainerContext* Ptr, INT64 Index, char* Elem)
	{
		return (char*)Ptr[Index].reference_ - Elem;

	}

	HyperContainerManager& vcmalloc_hcm = HyperContainerManager::getInstance();

	void										ExtendCcStorage(void* hypercontainer_address, SIZE_T containers_num) {
		HyperContainerContext* containerContext = vcmalloc_hcm(hypercontainer_address);
		if (!containerContext)
			throw std::string("Container not found");
		else {
			containerContext->AssertContainerContextStorage(containers_num);
		}
	}
	void										ExtendPfnsStorage(void* hypercontainer_address, SIZE_T size) {
		HyperContainerContext* containerContext = vcmalloc_hcm(hypercontainer_address);
		if (!containerContext)
			throw std::string("Container not found");
		else {
			containerContext->AssertPfnsStorage(size/page_size);
		}
	}
	template<typename DataType> static void		ContainerAlloc(HyperContainerContext* hypercontainer_context, PVOID hypercontainer_address, DataType*& user_pointer, SIZE_T elements_num) {
		
		ContainerContext*	lastStructContext	= hypercontainer_context->end();
		SIZE_T				requestedBytes		= sizeof(DataType) * elements_num;
		SIZE_T				lastGuestBytes		= 0;
		PVOID				mappingAddress		= nullptr;
		PVOID				nextAddress			= nullptr;
		
		if (hypercontainer_context->containers_num_ == 0) {
			mappingAddress	= hypercontainer_address;
			nextAddress		= hypercontainer_address;
		}
		else {
			mappingAddress	= lastStructContext->nextMappingAddress();
			nextAddress		= lastStructContext->nextAddress();
			lastGuestBytes	= lastStructContext->guestBytes();
		}
		
		ContainerContext	newContainerContext	= hypercontainer_context->insert((PVOID&)user_pointer, sizeof(DataType), elements_num);
		newContainerContext.reference_			= nextAddress;

		if (requestedBytes <= lastGuestBytes)
			return;

		SIZE_T	requestedPages	= (int)ceil((double)(requestedBytes - lastGuestBytes) / page_size);	//https://stackoverflow.com/a/65784221/10772859

		hypercontainer_context->AssertPfnsStorage(requestedPages);

		PULONG_PTR PFNs = hypercontainer_context->pfns_storage_ + hypercontainer_context->pfns_num_;
		SIZE_T allocatedPFNs = requestedPages;

		if (!AllocateUserPhysicalPages(GetCurrentProcess(), &allocatedPFNs, PFNs))
			throw std::string("vcmalloc: Physical Memory Allocation Error , Probably Physical Memory Unavailable");

		if(allocatedPFNs != requestedPages)
			throw std::string("vcmalloc: Physical Memory Allocation Incomplete , Probably Physical Memory Unavailable");

		if (requestedPages)
			if (!MapUserPhysicalPages(mappingAddress, requestedPages, PFNs))
				throw std::string("vcmalloc: MapUserPhysicalPages failed");

		hypercontainer_context->pfns_num_ += requestedPages;
		//newContainerContext.reference_ = nextAddress;

	}
	template<typename DataType> static void		ContainerRealloc(HyperContainerContext* hypercontainer_context, DataType*& user_pointer, SIZE_T newSize) {
		SIZE_T				PageSize				= page_size;
		ContainerContext*	concerned_container		= (*hypercontainer_context)[user_pointer]; if (concerned_container == nullptr)	throw std::string("vcrealloc: Data Structure not found");
		SIZE_T				startPFNsIdx			= hypercontainer_context->getPFNsPosition(hypercontainer_context->first(), concerned_container);
		SIZE_T				priorFreeBytes			= concerned_container->priorFreeBytes();
		SIZE_T				guestBytes				= concerned_container->guestBytes();
		INT64				requestedBytes			= newSize * sizeof(DataType) - priorFreeBytes + guestBytes; if (requestedBytes < 0) requestedBytes = 0;
		SIZE_T				requestedPages			= (SIZE_T)ceil((double)requestedBytes / PageSize);
		SIZE_T				concerned_container_pages	= concerned_container->nbrOfPages();

		//Indexers
		SIZE_T			containerPFNsIdx		= startPFNsIdx;
		INT64			steps					= 0;


		if (requestedPages < concerned_container_pages) {
			
			SIZE_T steps = concerned_container_pages - requestedPages;
			
			//Copy guest
			void* src = concerned_container->nextAddress();
			void* dst = (char*)concerned_container->nextAddress() - steps*PageSize;
			if(!memmove(dst, src, concerned_container->guestBytes()))
				throw std::string("structRealloc: Coudln't copy guest bytes");

			//Free frames
			SIZE_T freeIdx = containerPFNsIdx + requestedPages;
			SIZE_T pagesToFree = steps;
			if (!FreeUserPhysicalPages(GetCurrentProcess(), &pagesToFree, &hypercontainer_context->pfns_storage_[freeIdx]))
				throw std::string("structRealloc: couldn't free remaining frames");
			if(pagesToFree != steps)
				throw std::string("structRealloc: couldn't free all remaining frames");

			//Shift PFNs
			SIZE_T	PFNsToMove = hypercontainer_context->pfns_num_ - startPFNsIdx - concerned_container_pages;
			memmove(&hypercontainer_context->pfns_storage_[freeIdx], &hypercontainer_context->pfns_storage_[containerPFNsIdx + concerned_container_pages], PFNsToMove * sizeof(ULONG_PTR));

			//Remap
			if (!MapUserPhysicalPages(concerned_container->mappingAddress(), hypercontainer_context->pfns_num_ - startPFNsIdx, NULL))
				throw std::string("structRealloc: couldn't unmap previous mappings");
			if (!MapUserPhysicalPages(concerned_container->mappingAddress(), hypercontainer_context->pfns_num_ - startPFNsIdx - steps, hypercontainer_context->pfns_storage_ + startPFNsIdx))
				throw std::string("structRealloc: couldn't map comitted deallocation changes");

			//Update struct
			concerned_container->elements_num_ = (requestedPages * PageSize + concerned_container->priorFreeBytes() - concerned_container->guestBytes()) / sizeof(DataType);
			
			//Post-reallocation structs updates
			while (++concerned_container <= hypercontainer_context->end()) {
				(char*&)concerned_container->reference_ -= steps * PageSize;
			}

			//Container update
			hypercontainer_context->pfns_num_ -= steps;


		}
		else if (requestedPages > concerned_container_pages) {
			
			SIZE_T steps = requestedPages - concerned_container_pages;
			//Storage assertion
			hypercontainer_context->AssertPfnsStorage(steps);

			//Shift PFNs
			void* PFNsSrc = &hypercontainer_context->pfns_storage_[startPFNsIdx + concerned_container_pages];
			void* PFNsDst = &hypercontainer_context->pfns_storage_[startPFNsIdx + requestedPages];
			SIZE_T PFNsToMove = hypercontainer_context->pfns_num_ - startPFNsIdx - concerned_container_pages;
			memmove(PFNsDst, PFNsSrc, PFNsToMove*sizeof(ULONG_PTR));

			//Allocate needed PFNs
			SIZE_T pagesToAlloc = steps;
			if (!AllocateUserPhysicalPages(GetCurrentProcess(), &pagesToAlloc, (PULONG_PTR)PFNsSrc))
				throw std::string("structRealloc: couldn't allocate additional required frames");
			if(pagesToAlloc != steps)
				throw std::string("structRealloc: couldn't allocate enough additional required frames");

			//Remap
			if (hypercontainer_context->pfns_num_ != startPFNsIdx && !MapUserPhysicalPages(concerned_container->mappingAddress(), hypercontainer_context->pfns_num_ - startPFNsIdx, NULL))
				throw std::string("structRealloc: couldn't unmap previous mappings");
			if (!MapUserPhysicalPages(concerned_container->mappingAddress(), hypercontainer_context->pfns_num_ - startPFNsIdx + steps, &hypercontainer_context->pfns_storage_[startPFNsIdx]))
				throw std::string("structRealloc: couldn't map comitted deallocation changes");

			//Copy guest
			void* src = (char*)concerned_container->nextAddress();
			void* dst = (char*)concerned_container->nextAddress() + steps*PageSize;
			if(!memmove(dst, src, concerned_container->guestBytes()))
				throw std::string("structRealloc: couldn't couldn't copy guest bytes");

			//Update struct
			concerned_container->elements_num_ = (requestedPages * PageSize + concerned_container->priorFreeBytes() - concerned_container->guestBytes()) / sizeof(DataType);

			//Update post-reallocation structs
			while (++concerned_container <= hypercontainer_context->end()) {
				(char*&)concerned_container->reference_ += steps * PageSize;
			}

			//Container update
			hypercontainer_context->pfns_num_ += steps;

		}
	}
	template<typename DataType> static void		ContainerRealloc2(HyperContainerContext* hypercontainer_context, DataType*& user_pointer, SIZE_T* elements_num_array, SIZE_T array_size) {
		SIZE_T			requested_pages		=	0;
		ULONG_PTR*		sparePFNs			=	(ULONG_PTR*)HeapAlloc(GetProcessHeap(), 0, hypercontainer_context->pfns_num_ * sizeof(ULONG_PTR)); // To store spare PFNs
		ULONG_PTR*		tempPFNs			=	(ULONG_PTR*)HeapAlloc(GetProcessHeap(), 0, hypercontainer_context->pfns_num_ * sizeof(ULONG_PTR)); // Temporary helper PFNs array

		// Start conditions
		ContainerContext*	start_container		=	(*hypercontainer_context)[user_pointer]; if (start_container == nullptr)	throw std::string("vcrealloc: Data Structure not found");
		ContainerContext*	last_container		=	hypercontainer_context->end();
		ULONG_PTR			startPFNsIdx		=	hypercontainer_context->getPFNsPosition(hypercontainer_context->first(), start_container);
		SIZE_T				startTempPFNsIdx	=	0;
		SIZE_T				startSteps			=	0;
		SIZE_T				startNumberOfPFNs	=	hypercontainer_context->pfns_num_;

		// Indexers
		ContainerContext*	itStruct			=	start_container;
		ULONG_PTR			PFNsIdx				=	startPFNsIdx;
		SIZE_T				tempPFNsIdx			=	startTempPFNsIdx;
		SIZE_T				steps				=	startSteps;

		// Globals to be calculated later
		SIZE_T numberOfPostReallocPages		=	0;
		
		for (SIZE_T i = 0; i < array_size; i++) {

			
			INT64	number_of_containers			=	elements_num_array[i];
			SIZE_T	container_pages		=	itStruct->nbrOfPages();
			
			if (number_of_containers == itStruct->elements_num_ || number_of_containers == -1) {
				
				
				memmove(tempPFNs + tempPFNsIdx, hypercontainer_context->pfns_storage_ + PFNsIdx, container_pages * sizeof(ULONG_PTR));

				PFNsIdx							+=	container_pages;
				tempPFNsIdx						+=	container_pages;
				(char*&)itStruct->reference_	-=	steps * page_size;
				itStruct						+=	1;

				continue;
			}

			SIZE_T	itStructPriorFreeBytes	= itStruct->priorFreeBytes();
			SIZE_T	itStructGuestBytes		= itStruct->guestBytes();

			INT64	requestedBytes			=	number_of_containers * sizeof(DataType) - itStructPriorFreeBytes + itStructGuestBytes; if(requestedBytes < 0) requestedBytes = 0;
			SIZE_T	requestedNumberOfPages	=	(SIZE_T)ceil((double)requestedBytes / page_size);
			
			if (requestedNumberOfPages	==	container_pages) {
				memmove(tempPFNs + tempPFNsIdx, hypercontainer_context->pfns_storage_ + PFNsIdx, container_pages * sizeof(ULONG_PTR));

				PFNsIdx						+=	container_pages;
				tempPFNsIdx					+=	container_pages;
				(char*&)itStruct->reference_ -=	steps * page_size;
				itStruct					+=	1;

				continue;
			}
			if (requestedNumberOfPages	>	container_pages){
				requested_pages += requestedNumberOfPages - container_pages;
				
				memmove(tempPFNs + tempPFNsIdx, hypercontainer_context->pfns_storage_ + PFNsIdx, container_pages * sizeof(ULONG_PTR));

				PFNsIdx						+=	container_pages;
				tempPFNsIdx					+=	container_pages;
				(char*&)itStruct->reference_ -=	steps * page_size;
				itStruct					+=	1;

				continue;
			}
			if (requestedNumberOfPages	<	container_pages){

				PVOID itStructNextMappingAddress	=	itStruct->nextMappingAddress();
				SIZE_T itStructGuestBytes			=	itStruct->guestBytes();

				SIZE_T itSteps		=	container_pages - requestedNumberOfPages;
				PVOID source		=	(char*)itStructNextMappingAddress - itStructGuestBytes;
				PVOID destination	=	(char*)source - (itSteps) * page_size;
				
				memmove(destination,			source, itStructGuestBytes);
				memmove(tempPFNs + tempPFNsIdx, hypercontainer_context->pfns_storage_ + PFNsIdx,							requestedNumberOfPages * sizeof(ULONG_PTR));
				memmove(sparePFNs + steps,		hypercontainer_context->pfns_storage_ + PFNsIdx + requestedNumberOfPages, itSteps * sizeof(ULONG_PTR));

				PFNsIdx							+=	container_pages;
				tempPFNsIdx						+=	requestedNumberOfPages;
				itStruct->elements_num_				 =	(requestedNumberOfPages*page_size + itStructPriorFreeBytes - itStructGuestBytes) / sizeof(DataType);
				(char*&)itStruct->reference_		-=	steps * (page_size);
				steps							+=	itSteps;
				hypercontainer_context->pfns_num_	-=	itSteps;
				elements_num_array[i]		 =	-1;
				itStruct++;

			}

		}


		while (itStruct <= last_container) {
			(char*&)itStruct->reference_		-=	steps*(page_size);
			itStruct++;
		}

		numberOfPostReallocPages			= startNumberOfPFNs - PFNsIdx;
		SIZE_T numberofPagesToCommit		= tempPFNsIdx + numberOfPostReallocPages;
		memmove(tempPFNs + tempPFNsIdx,						hypercontainer_context->pfns_storage_ + PFNsIdx,		numberOfPostReallocPages * sizeof(ULONG_PTR));
		memmove(hypercontainer_context->pfns_storage_ + startPFNsIdx,		tempPFNs,								(numberofPagesToCommit) * sizeof(ULONG_PTR));
		
		if(startNumberOfPFNs != startPFNsIdx && !MapUserPhysicalPages(start_container->mappingAddress(), startNumberOfPFNs - startPFNsIdx, NULL))
			throw std::string("vcrealloc: couldn't unmap previous mappings");

		if (numberofPagesToCommit && !MapUserPhysicalPages(start_container->mappingAddress(), numberofPagesToCommit, hypercontainer_context->pfns_storage_ + startPFNsIdx))
			throw std::string("vcrealloc: couldn't map comitted deallocation changes");


		if (!HeapFree(GetProcessHeap(), 0, tempPFNs))
			throw std::string("vcrealloc: couldn't free the temporary PFNs array");

		INT64 remainingPages = steps - requested_pages;

		if (remainingPages > 0) {
			SIZE_T pagesToFree = remainingPages;
			if (FreeUserPhysicalPages(GetCurrentProcess(), &pagesToFree, sparePFNs + requested_pages) == FALSE)
				throw std::string("vcrealloc: couldn't free remaining frames");

			if (pagesToFree != remainingPages)
				throw std::string("vcrealloc: couldn't free all remaining frames");
		}

		if (requested_pages > 0) {
			
			if (remainingPages < 0) {

				sparePFNs = (ULONG_PTR*)HeapReAlloc(GetProcessHeap(), 0, sparePFNs, (steps + -remainingPages) * sizeof(ULONG_PTR));

				SIZE_T pagesToAlloc = -remainingPages;
				if (AllocateUserPhysicalPages(GetCurrentProcess(), &pagesToAlloc, sparePFNs + steps) == FALSE)
					throw std::string("vcrealloc: couldn't allocate additional required frames");

				if (pagesToAlloc + remainingPages != 0)
					throw std::string("vcrealloc: couldn't allocate enough additional required frames");
			}


			tempPFNs				=	(ULONG_PTR*)HeapAlloc(GetProcessHeap(), 0, (hypercontainer_context->pfns_num_ + requested_pages) * sizeof(ULONG_PTR));
			PVOID copy_buffer		=	VirtualAlloc(NULL, page_size, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);


			// Indexers
			itStruct		=	start_container;
			PFNsIdx			=	startPFNsIdx;
			tempPFNsIdx		=	startTempPFNsIdx;
			steps			=	startSteps;

			hypercontainer_context->AssertPfnsStorage(hypercontainer_context->pfns_num_ + requested_pages);
			//Successive allocation loop
			for (SIZE_T i = 0; i < array_size; i++) {
				INT64	numberOfStructs = elements_num_array[i];
				SIZE_T	container_pages_number = itStruct->nbrOfPages();

				if (numberOfStructs == itStruct->elements_num_ || numberOfStructs == -1) {
				
					memmove(tempPFNs + tempPFNsIdx, hypercontainer_context->pfns_storage_ + PFNsIdx, container_pages_number * sizeof(ULONG_PTR));

					PFNsIdx							+=	container_pages_number;
					tempPFNsIdx						+=	container_pages_number;
					(char*&)itStruct->reference_	-=	steps * page_size;
					itStruct						+=	1;

					continue;
				}

				SIZE_T	itStructPriorFreeBytes = itStruct->priorFreeBytes();
				SIZE_T	itStructGuestBytes = itStruct->guestBytes();

				INT64	requestedBytes = numberOfStructs * sizeof(DataType) - itStructPriorFreeBytes + itStructGuestBytes; if (requestedBytes < 0) requestedBytes = 0;
				SIZE_T	requestedNumberOfPages = (SIZE_T)ceil((double)requestedBytes / page_size);

				if (requestedNumberOfPages	==	container_pages_number) {
					memmove(tempPFNs + tempPFNsIdx, hypercontainer_context->pfns_storage_ + PFNsIdx, container_pages_number * sizeof(ULONG_PTR));

					PFNsIdx							+=	container_pages_number;
					tempPFNsIdx						+=	container_pages_number;
					(char*&)itStruct->reference_	-=	steps * page_size;
					itStruct						+=	1;

					continue;
				}
				if (requestedNumberOfPages	>	container_pages_number){

					PVOID	itStructNextMappingAddress	=	itStruct->nextMappingAddress();
					SIZE_T	itStructLastPageOffset		=	itStruct->lastPageOffset();


					SIZE_T itSteps		=	requestedNumberOfPages - container_pages_number;
					PVOID source		=	(char*)itStructNextMappingAddress - itStructGuestBytes;
					PVOID destination	=	(char*)copy_buffer + itStructLastPageOffset;
					
					if (!MapUserPhysicalPages(copy_buffer, 1, sparePFNs + requested_pages - itSteps))
						throw std::string("vcrealloc: failed to map guest destination");

					memmove(destination,										source,									itStructGuestBytes);
					memmove(tempPFNs + tempPFNsIdx,								hypercontainer_context->pfns_storage_ + PFNsIdx,		container_pages_number * sizeof(ULONG_PTR));
					memmove(tempPFNs + tempPFNsIdx + container_pages_number,	sparePFNs + requested_pages - itSteps,	itSteps * sizeof(ULONG_PTR));

					if (!MapUserPhysicalPages(copy_buffer, 1, NULL))
						throw std::string("vcrealloc: failed to ummap guest destination");

					PFNsIdx								+=	container_pages_number;
					tempPFNsIdx							+=	requestedNumberOfPages;
					itStruct->elements_num_				 =	(requestedNumberOfPages*page_size + itStructPriorFreeBytes - itStructGuestBytes) / sizeof(DataType);
					(char*&)itStruct->reference_		+=	steps * (page_size);
					requested_pages						-=	itSteps;
					steps								+=	itSteps;
					hypercontainer_context->pfns_num_	+=	itSteps;
					itStruct++;

				}
			}



			//Update post reallocation structs info
			while (itStruct <= last_container) {
				(char*&)itStruct->reference_ += steps * (page_size);
				itStruct++;
			}

			SIZE_T numberofPagesToCommit = tempPFNsIdx + numberOfPostReallocPages;

			//Copy post reallocation pages
			memmove(tempPFNs + tempPFNsIdx, hypercontainer_context->pfns_storage_ + PFNsIdx, numberOfPostReallocPages * sizeof(ULONG_PTR));

			//Commit changes
			memmove(hypercontainer_context->pfns_storage_ + startPFNsIdx, tempPFNs, (numberofPagesToCommit) * sizeof(ULONG_PTR));




			if(startNumberOfPFNs != startPFNsIdx && !MapUserPhysicalPages(start_container->mappingAddress(), startNumberOfPFNs - startPFNsIdx, NULL))
				throw std::string("vcrealloc: couldn't unmap old frames upon allocation");

			if (numberofPagesToCommit && !MapUserPhysicalPages(start_container->mappingAddress(), numberofPagesToCommit, hypercontainer_context->pfns_storage_ + startPFNsIdx))
					throw std::string("vcrealloc: couldn't map comitted allocation changes");

			if (!HeapFree(GetProcessHeap(), 0, tempPFNs))
				throw std::string("vcrealloc: couldn't free the temporary PFNs array");

			if (!VirtualFree(copy_buffer, 0, MEM_RELEASE))
				throw std::string("vcrealloc: failed to release virtuall buffer");
			

			//clock_t start = clock();
			//std::cout << "VCREALLOC: " << ((double)clock() - start) / CLOCKS_PER_SEC << std::endl;
		}

		if (!HeapFree(GetProcessHeap(), 0, sparePFNs))
			throw std::string("vcrealloc: couldn't free the spare PFNs array");

	}
	static void									HyperContainerFree(PVOID hypercontainer_address, HyperContainerManager* cm = &vcmalloc_hcm){
		INT64 hyper_container_position = Search::binarySearch(cm->hypercontainers_num_, (char**&)cm->pointers_storage_, (char*&)hypercontainer_address);
		if(hyper_container_position == -1)
			throw std::string("vcfree: container not found");
		
		HyperContainerContext* hcc = &cm->hyper_containers_storage_[hyper_container_position];
		
		SIZE_T frames_to_free = hcc->pfns_num_;

		if (frames_to_free && FreeUserPhysicalPages(GetCurrentProcess(), &frames_to_free, hcc->pfns_storage_) == FALSE)
			throw std::string("vcfree: couldn't free container frames");
		if (frames_to_free != hcc->pfns_num_)
			throw std::string("vcfree: couldn't free all container's frames");

		if(!VirtualFree(hypercontainer_address, NULL, MEM_RELEASE))
			throw std::string("vcfree: couldn't free hypercontainer's virtual space");

		free(hcc->containercontext_storage_);
		HeapFree(GetProcessHeap(), 0, hcc->pfns_storage_);

		memmove(&cm->hyper_containers_storage_[hyper_container_position], &cm->hyper_containers_storage_[hyper_container_position + 1], sizeof(HyperContainerContext) * (cm->hypercontainers_num_ - (hyper_container_position + 1)));
		memmove(&cm->pointers_storage_[hyper_container_position], &cm->pointers_storage_[hyper_container_position + 1], sizeof(ULONG_PTR) * (cm->hypercontainers_num_ - (hyper_container_position + 1)));


		cm->hypercontainers_num_--;


	}
	static PVOID								vccalloc(SIZE_T dwSize)
	{
		SYSTEM_INFO sysInf;
		GetSystemInfo(&sysInf);
		DWORD aG = sysInf.dwAllocationGranularity;

		PVOID requestedAddress = NULL;
		if (vcmalloc_hcm.hypercontainers_num_) {
			PVOID lastAddress = vcmalloc_hcm.pointers_storage_[vcmalloc_hcm.hypercontainers_num_ - 1];
			SIZE_T lastSize = vcmalloc_hcm.hyper_containers_storage_[vcmalloc_hcm.hypercontainers_num_ - 1].hypercontainer_size_;
			requestedAddress =  (char*)lastAddress + lastSize;
		}
		
		PVOID lpMemReserved = NULL;

		lpMemReserved = VirtualAlloc(requestedAddress, dwSize, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);

		while (!lpMemReserved) {
			(char*&)requestedAddress += aG;
			lpMemReserved = VirtualAlloc(requestedAddress, dwSize, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);
		}

		SIZE_T containerSize = dwSize / aG;
		if (dwSize % aG)
			containerSize++;
		containerSize *= aG;

		vcmalloc_hcm.insert(lpMemReserved, containerSize);
		
		return lpMemReserved;
	};
	template<typename DataType> static void		vcmalloc(PVOID hypercontainer_address, DataType*& user_pointer, SIZE_T elements_num) {
		HyperContainerContext* containerContext = vcmalloc_hcm(hypercontainer_address);
		if (containerContext)
			ContainerAlloc(containerContext, hypercontainer_address, user_pointer, elements_num);
		else
			throw std::string("Container not found");
	}
	template<typename DataType> static void		vcrealloc(PVOID hypercontainer_address, DataType*& user_pointer, SIZE_T elements_new_num) {
		HyperContainerContext* hyperContainerContext = vcmalloc_hcm(hypercontainer_address);
		if (hyperContainerContext)
			ContainerRealloc(hyperContainerContext, user_pointer, elements_new_num);
		else
			throw std::string("Container not found");
	}
	template<typename DataType> static void		vcrealloc2(PVOID hypercontainer_address, DataType*& user_pointer, SIZE_T* elements_num_array, SIZE_T array_size) {
		HyperContainerContext* containerContext = vcmalloc_hcm(hypercontainer_address);
		if (containerContext)
			ContainerRealloc2(containerContext, user_pointer, elements_num_array, array_size);
		else
			throw std::string("Container not found");
	}
	static void									vccfree(PVOID hypercontainer_address, HyperContainerManager* cm = &vcmalloc_hcm){
		HyperContainerFree(hypercontainer_address, &vcmalloc_hcm);
	}

}

