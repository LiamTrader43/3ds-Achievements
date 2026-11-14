#include "rewards.h"

#include <stdio.h>
#include <string.h>

#define RECORD_PATH     "/achievements/play_coins.txt"
#define RECORD_TEMP     RECORD_PATH ".tmp"

// gamecoin.dat: 0x14 bytes; the total Play Coins are the u16 at offset 4. The rest (coins
// and steps today, the date of the last coin) is the pedometer's and is left alone.
#define COIN_EXTDATA_ID 0xF000000B
#define COIN_FILE       "/gamecoin.dat"
#define COIN_FILE_SIZE  0x14
#define COINS_OFFSET    4

// What's been given before (both 0 if the file isn't there yet).
static void ReadRecord(int *rewards, int *owed)
{
    char line[96];
    int  value;

    *rewards = 0, *owed = 0;
    FILE *f = fopen(RECORD_PATH, "r");
    if (f == NULL)
        return;
    while (fgets(line, sizeof(line), f) != NULL)
    {
        if (sscanf(line, "rewards %d", &value) == 1 && value >= 0)
            *rewards = value;
        else if (sscanf(line, "owed %d", &value) == 1 && value >= 0)
            *owed = value;
    }
    fclose(f);
}

// Written whole to a new file, then put in place of the old one.
static bool WriteRecord(int rewards, int owed)
{
    FILE *f = fopen(RECORD_TEMP, "w");
    if (f == NULL)
        return false;
    fprintf(f, "# Play Coins from achievements (3DS Achievements): %d for every %d points\n",
            REWARD_COINS, REWARD_POINTS);
    fprintf(f, "rewards %d\nowed %d\n", rewards, owed);
    bool ok = fclose(f) == 0;
    if (!ok)
    {
        remove(RECORD_TEMP);
        return false;
    }
    remove(RECORD_PATH);
    return rename(RECORD_TEMP, RECORD_PATH) == 0;
}

// Adds up to add Play Coins, never past PLAY_COINS_MAX: *added how many were, *coins the
// total now (-1 if it couldn't be read). Only the two bytes of the total are written, and
// only when something is added.
static Result AddCoins(int add, int *added, int *coins)
{
    u32     id[3] = { MEDIATYPE_NAND, COIN_EXTDATA_ID, 0x00048000 };
    FS_Path path = { PATH_BINARY, sizeof(id), id };
    FS_Archive archive;
    Handle  file;

    *added = 0, *coins = -1;
    Result res = FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, path);
    if (R_FAILED(res))
        return res;
    res = FSUSER_OpenFile(&file, archive, fsMakePath(PATH_ASCII, COIN_FILE), FS_OPEN_READ | FS_OPEN_WRITE, 0);
    if (R_SUCCEEDED(res))
    {
        u8  data[COIN_FILE_SIZE];
        u32 read = 0;
        u64 size = 0;
        res = FSFILE_GetSize(file, &size);
        if (R_SUCCEEDED(res))
            res = FSFILE_Read(file, &read, 0, data, sizeof(data));
        if (R_SUCCEEDED(res) && (size != COIN_FILE_SIZE || read != COIN_FILE_SIZE))
            res = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, RD_INVALID_SIZE);   // not the file we know
        if (R_SUCCEEDED(res))
        {
            int now = data[COINS_OFFSET] | data[COINS_OFFSET + 1] << 8;
            int room = now < PLAY_COINS_MAX ? PLAY_COINS_MAX - now : 0;
            int give = add < room ? add : room;
            if (give > 0)
            {
                u16 total = (u16)(now + give);
                u8  bytes[2] = { (u8)(total & 0xFF), (u8)(total >> 8) };
                u32 written = 0;
                res = FSFILE_Write(file, &written, COINS_OFFSET, bytes, sizeof(bytes), FS_WRITE_FLUSH);
                if (R_SUCCEEDED(res) && written != sizeof(bytes))
                    res = MAKERESULT(RL_PERMANENT, RS_INVALIDSTATE, RM_APPLICATION, RD_INVALID_SIZE);
                if (R_SUCCEEDED(res))
                    now = total, *added = give;
            }
            *coins = now;
        }
        FSFILE_Close(file);
    }
    FSUSER_CloseArchive(archive);
    return res;
}

int Rewards_Coins(void)
{
    u32     id[3] = { MEDIATYPE_NAND, COIN_EXTDATA_ID, 0x00048000 };
    FS_Path path = { PATH_BINARY, sizeof(id), id };
    FS_Archive archive;
    Handle  file;
    int     coins = -1;

    if (R_FAILED(FSUSER_OpenArchive(&archive, ARCHIVE_SHARED_EXTDATA, path)))
        return -1;
    if (R_SUCCEEDED(FSUSER_OpenFile(&file, archive, fsMakePath(PATH_ASCII, COIN_FILE), FS_OPEN_READ, 0)))
    {
        u8  data[COIN_FILE_SIZE];
        u32 read = 0;
        u64 size = 0;
        if (R_SUCCEEDED(FSFILE_GetSize(file, &size)) && size == COIN_FILE_SIZE &&
            R_SUCCEEDED(FSFILE_Read(file, &read, 0, data, sizeof(data))) && read == COIN_FILE_SIZE)
            coins = data[COINS_OFFSET] | data[COINS_OFFSET + 1] << 8;
        FSFILE_Close(file);
    }
    FSUSER_CloseArchive(archive);
    return coins;
}

bool Rewards_Update(int points, Rewards *out)
{
    int before, owed;

    memset(out, 0, sizeof(*out));
    out->coins = -1;
    ReadRecord(&before, &owed);
    const int owedBefore = owed;

    // Rewards reached so far; never fewer than given before (achievements can be reset or
    // removed, and a reward isn't taken back or given twice).
    int rewards = points > 0 ? points / REWARD_POINTS : 0;
    if (rewards < before)
        rewards = before;
    out->rewards = rewards;
    out->earned = (rewards - before) * REWARD_COINS;
    owed += out->earned;
    if (owed == 0 && out->earned == 0)
        return false;               // nothing new, nothing kept: the coins aren't touched

    // The coins first, then the record: if the record can't be written, the worst is a
    // reward given again, never one lost.
    int given = 0, coins = -1;
    out->result = AddCoins(owed, &given, &coins);
    out->given = given;
    out->coins = coins;
    out->owed = owed - given;
    if (rewards != before || out->owed != owedBefore)
        WriteRecord(rewards, out->owed);
    return out->earned > 0 || out->given > 0;
}
