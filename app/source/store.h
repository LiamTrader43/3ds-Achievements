#ifndef STORE_H
#define STORE_H

// The store: things to buy with the system's Play Coins (rewards.c gives them for achievement
// points). What's for sale is a list in the store's files (store_file.h: items.bin, from
// app/store/items.txt), which arrive from the repository like games' files, so items are added
// without a new app. What's been bought is kept in /achievements/purchases.txt, one item ID per
// line, so a purchase stays bought. An item can install an add-on theme: its file is a store
// file too, and is put in /achievements/themes when it's bought.

#include <3ds.h>

typedef struct
{
    const char *id;             // saved in purchases.txt: never change it once it's for sale
    const char *name;
    const char *description;    // one or two lines
    int         price;          // Play Coins
    const u16  *icon;           // 48x48 like a trophy, or NULL for the store's bag (always, for now)
    const char *theme;          // the add-on theme it installs (/achievements/store/<theme>.bin to
                                // /achievements/themes/<theme>.theme), or NULL
} StoreItem;

// Store_Buy: purchases.txt couldn't be written (nothing was spent).
#define STORE_NOT_SAVED     MAKERESULT(RL_PERMANENT, RS_CANCELED, RM_APPLICATION, RD_NO_DATA)

// Reads what's for sale and which items have been bought. Call before the others, and again
// when the store's files have changed (Store_Item's pointers from before are then no good).
void             Store_Load(void);

// Puts back what bought items installed, from their store files, if it's gone (a theme file
// deleted), can't be used or came from an older store file (the theme was updated). Call after
// Store_Load, before the themes are read.
void             Store_Restore(void);

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
