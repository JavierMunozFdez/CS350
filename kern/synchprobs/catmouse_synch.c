#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>
#include <array.h>


/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct cv *noCatsEating;
static struct cv *noMiceEating;
static struct lock** locks;
static volatile int catEating = 0;
static volatile  int mouseEating = 0;

/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{
  	noCatsEating = cv_create("noCatsEatingCV");
	noMiceEating = cv_create("noMiceEatingCV");
	locks = kmalloc(sizeof(struct lock *)* bowls);
	for(int i = 0; i < bowls; i++){
//		char str[15];
//		snprintf(str, "%d", i);
		locks[i] = lock_create("");
	}
	return;
}

/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
	//(void)bowls;
	for(int i = 0; i < bowls; i++){
                lock_destroy(locks[i]);
        }
	cv_destroy(noMiceEating);
	cv_destroy(noCatsEating);

}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl) 
{
	int _bowl = bowl - 1;
	struct lock* lk = locks[_bowl];
	// this bowl is now free to eat from for this cat
//	kprintf("Cat trying to acquire lock %d \n", bowl);
	lock_acquire(lk);
	// sleep until mice are still eating
//	kprintf("Cat trying to eat from bowl %d after acquiring lock\n", bowl);

	while(mouseEating != 0){
		cv_wait(noMiceEating, lk);
	}
	catEating = catEating + 1;

//	kprintf("Cat going to eat now from bowl %d after waiting for mice \n", bowl);
//	kprintf("Cats currently eating:  %d \n", catEating);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
	catEating = catEating - 1;

//	kprintf("Cat wakes up after eating from bowl %d \n", bowl);
        int _bowl = bowl - 1;
        struct lock* lk =locks[_bowl];
//	kprintf("Cats that are still eating: %d \n", catEating);
	if(catEating == 0){
//		kprintf("Waking up a sleeping mouse because cats 0 cats eating \n");
		cv_broadcast(noCatsEating,lk);
	}
	lock_release(lk);

}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
	int _bowl = bowl - 1;
        struct lock* lk =locks[_bowl];
	// this bowl is now free to eat from for this cat
        //kprintf("Mouse trying to acquire lock %d \n", bowl);
	lock_acquire(lk);
//	kprintf("Mouse trying to eat from bowl %d after acquiring lock\n", bowl);
        // sleep until mice are still eating

        while(catEating > 0){
                cv_wait(noCatsEating, lk);
        }
        mouseEating = mouseEating + 1;

//	kprintf("Mouse going to eat now from bowl %d after waiting for cats \n", bowl);
  //      kprintf("Mice currently eating:  %d \n", catEating);

}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{
        mouseEating = mouseEating - 1;

	int _bowl = bowl - 1;
        struct lock* lk =locks[_bowl];
//	kprintf("Mouse wakes up after eating from bowl %d \n", bowl);
//	kprintf("Mice that are still eating: %d \n", mouseEating);

        if(mouseEating == 0){
//		kprintf("Waking up a sleeping cat  because mouse  0 mice eating \n");
                cv_broadcast(noMiceEating,lk);
        }

	lock_release(lk);

}
