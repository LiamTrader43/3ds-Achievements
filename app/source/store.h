#ifndef STORE_H
#define STORE_H

// The store: things to buy with the system's Play Coins (rewards.c gives them for achievement
// points). What's for sale is the list in store.c. What's been bought is kept in
// /achievements/purchases.txt, one item ID per line, so a purchase stays bought.

#include <3ds.h>

typedef struct
{
    const char *id;             // saved in purchases.txt: never change it once it's for sale
    const char *name;
    const char *description;    // one or two lines
    int         price;          // Play Coins
    const u16  *icon;           // 48x48 like a trophy (store_icon.h), or NULL for the store's bag
} StoreItem;

// Store_Buy: purchases.txt couldn't be written (nothing was spent).
#define STORE_NOT_SAVED     MAKERESULT(RL_PERMANENT, RS_CANCELED, RM_APPLICATION, RD_NO_DATA)

// Reads which items have been bought. Call once before the others.
void             Store_Load(void);

// What's for sale, in the order the store lists it.
int              Store_Count(void);
const StoreItem *Store_Item(int i);

// Whether item i, or the item with this ID, has been bought (for what the items unlock).
bool             Store_Owned(int i);
bool             Store_OwnedId(const char *id);
int              Store_OwnedCount(void);

// Buys item i: saves it as bought, then takes its price from the system's Play Coins. If the
// coins can't be taken (REWARDS_NOT_ENOUGH, or an error), it's saved as not bought again.
// *coins: the Play Coins after (-1 if they couldn't be read).
Result           Store_Buy(int i, int *coins);

#endif
