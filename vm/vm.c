/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* P3-1 추가 */
bool insert_page (struct hash *pages, struct page *p);
bool delete_page (struct hash *pages, struct page *p);
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED);
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
struct list frame_table; 

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;// NULL 받아오고
	/* TODO: Fill this function. */
    struct page* page = (struct page*)malloc(sizeof(struct page)); // 페이지 있는건 사이즈 할당 해서 가져오고
    struct hash_elem *e; // 요소도 받아온다

    page->va = pg_round_down(va);  // va가 가리키는 가상 페이지의 시작 포인트(오프셋이 0으로 설정된 va) 반환
    e = hash_find(&spt->pages, &page->hash_elem); // hash_find로 hash_elem 구조체를 e로 받아온다

    free(page);

    return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
	// return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	int succ = false;
	/* P3 추가 */
	// return succ; // insert_page를 리턴하면 succ은 언제 쓰지?
	return insert_page(&spt->pages, page);
}

/* P3 추가 */
bool insert_page (struct hash *pages, struct page *p) {
	if (hash_insert(pages, &p->hash_elem))
		return false; // spt_insert_page함수에서 succ = false 니까
	else
		return true;
}

/* P3 추가 */
bool delete_page (struct hash *pages, struct page *p) {
	if (hash_delete(pages, &p->hash_elem))
		return false;
	else
		return true;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	
	/* P3 추가 */
	// struct thread *curr = thread_current();
    // struct list_elem *e = start;

    // for (start = e; start != list_end(&frame_table); start = list_next(start)) {
    //     victim = list_entry(start, struct frame, frame_elem);
    //     if (pml4_is_accessed(curr->pml4, victim->page->va))
    //         pml4_set_accessed (curr->pml4, victim->page->va, 0);
    //     else
    //         return victim;
    // }

    // for (start = list_begin(&frame_table); start != e; start = list_next(start)) {
    //     victim = list_entry(start, struct frame, frame_elem);
    //     if (pml4_is_accessed(curr->pml4, victim->page->va))
    //         pml4_set_accessed (curr->pml4, victim->page->va, 0);
    //     else
    //         return victim;
    // }
	victim = list_entry(list_pop_front(&frame_table), struct frame, elem); // FIFO algorithm
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page); /* P3 추가 */
	return NULL;
}

/* palloc() and get frame. 
	If there is no available page, evict the page and return it. 
	This always return valid address. 
	That is, if the user pool memory is full, 
	this function evicts the frame to get the available memory space.*/
static struct frame *vm_get_frame (void) {
	struct frame *frame = NULL;
	void *kva = palloc_get_page(PAL_USER);
	/* TODO: Fill this function. */

	/* P3 추가 */
	if (kva == NULL){ // NULL이면(사용 가능한 페이지가 없으면) 
		frame = vm_evict_frame(); // 페이지 삭제 후 frame 리턴
	}
	else{ // 사용 가능한 페이지가 있으면
		frame = malloc(sizeof(struct frame)); // 페이지 사이즈만큼 메모리 할당
		frame->kva = kva;
	}
	
	ASSERT (frame != NULL);
	// ASSERT (frame->page == NULL);
	return frame;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	// 페이지를 va에 할당해라..

	// 일단 NULL인 페이지 구조체 포인터 하나를 가져왔고
	// 이 페이지를 va에 할당..
	// va를 ..

	// 여기서 할당 준비해서 do_claim으로 보내서 페이지테이블에 맵핑
	// 이 주제의 목적인 supplemental_page_table에서 찾기 구현을 여기서 하면 됨!!
	struct supplemental_page_table *spt = &thread_current()->spt; // 이러면 주소를 가리키는건가?
	struct page *page = spt_find_page(spt, va);
	if (page == NULL) {
		return false;
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/* va에서 PT(안의 pa)에 매핑을 추가함. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();
	/* P3 추가 */
	struct thread *cur = thread_current();
	bool writable = page->writable; // [vm.h] struct page에 bool writable; 추가

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* P3 추가 */
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (!pml4_set_page(cur->pml4, page->va, frame->kva, writable)) {
		return false // 최종 반환 값은 성공 여부를 나타내야 한다고 했으니까!
	}
	return swap_in (page, frame->kva);
	
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->pages, page_hash, page_less, NULL);
}
// '추가'페이지 테이블 이니까, 일반 페이지 테이블 init처럼 시작하면 되지 않을까? 근데 일반 페이지테이블 함수는 어디있을깡
// struct page의 union sturct 중 하나를 사용해야 하나?
/* 페이지 폴트 및 리소스 관리를 처리하기 위해 각 페이지에 대한 추가 정보를 보유하는 추가 페이지 테이블도 필요합니다. */
/* 추가 페이지 테이블에 사용할 데이터 구조를 선택할 수 있습니다. -> 비트맵 or 해시테이블 구조로 짜면 된다. */
// 자료형 초깃값, 멤버변수들 넣기. struct도 채워야 함


/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

/* P3 추가 */
// 해시 테이블 초기화할 때 해시 값을 구해주는 함수의 포인터
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED) {
    const struct page *p = hash_entry(p_, struct page, hash_elem);
    return hash_bytes(&p->va, sizeof p->va);
}

// 해시 테이블 초기화할 때 해시 요소들 비교하는 함수의 포인터
// a가 b보다 작으면 true, 반대면 false
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
    const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);

    return a->va < b->va;
}

