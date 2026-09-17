// RaidEnemies.cpp - putting the level's enemies in the arena.
//
// CUSTOM.
//
// WHY THIS IS NOT ONE CALL INTO THE ENGINE
//
// An enemy in this game is made in two places that never meet:
//
//   cmd_enemy_set (CmdFunctions.cpp)   fills an entity slot. It is an SCD
//     opcode: it reads its arguments out of the script stream through
//     g_ScdOpcodes, so it cannot be called with parameters.
//
//   room_set (RoomInit.cpp)            loads the models, in a loop that runs
//     over the entities the script has just created. It is part of init_room,
//     and by the time a RAID level is read it has already gone by.
//
// So both halves are done here, from the level file instead of from a script.
// The field list below is cmd_enemy_set's, in its order, so a future change
// there is easy to mirror; the loading loop is room_set's, with the same
// model-reuse shortcut for consecutive enemies of one kind.
//
// THE ONE FIELD THAT IS NOT OBVIOUS
//
//   ENTITY->Sca_info = g_scaDataTable[0];
//
// is the VALUE of the first record, not the address of the table. The original
// binary gets away with the ambiguity because its table sits at 0x004d4540, so
// reading the address as a record yields a collision radius of 77. In this port
// the addresses are much larger and the same mistake produced a radius of
// 22133, which teleported anything it touched across the room. The comment at
// CmdFunctions.cpp:900 tells that story at length; it is repeated here because
// this is the only other place in the codebase that writes the field.
#include "../Globals.h"
#include "Types.h"
#include "Entities.h"
#include "RaidLevel.h"
#include "RaidEnemies.h"
#include <cstring>

extern void LoadEntityEMD(Entity* em, unsigned char entity_id);   // EntityModelLoader.cpp
extern void Entity_SetJoints(Entity* em, unsigned int stride);    // EntityModelLoader.cpp
extern void InitAnimStructure(void* animHeaderValue);             // EntityModelLoader.cpp
extern unsigned int SetupJointStructures(unsigned int base);      // EntityModelLoader.cpp

// The script only ever uses slots 0..15 of the 30, and so does this: the upper
// half is where the engine puts things that are not script enemies.
#define RAID_ENEMY_SLOTS   16

// Enemy ids run 0..21 in the EMD path table (0..3 are the player characters,
// which is why the file index is id + 4). Anything else has no model and is
// skipped rather than loaded as whatever happens to sit at that offset.
#define RAID_ENEMY_MAX_ID  21

// How much collision-part pool each one takes. The script passes a per-enemy
// count; a level file does not have one, so this is a fixed, generous slice.
// Over-reserving costs pool; under-reserving lets two enemies share hit data.
#define RAID_ENEMY_SCA_PARTS 8

static int s_spawned = 0;

void RaidEnemies_Clear(void)
{
    if (!s_spawned) return;
    for (int i = 0; i < RAID_ENEMY_SLOTS; i++) {
        memset(&g_EnemiesList[i], 0, sizeof(g_EnemiesList[i]));
    }
    g_enemy_count = 0;
    s_spawned = 0;
}

void RaidEnemies_Spawn(void)
{
    if (!g_raidLevel.loaded) return;

    RaidEnemies_Clear();
    if (g_raidLevel.nenemy <= 0) return;

    Entity* const saveEntity = (Entity*)ENTITY;
    int placed = 0;

    // ---- the fields, as cmd_enemy_set writes them --------------------------
    for (int i = 0; i < g_raidLevel.nenemy && placed < RAID_ENEMY_SLOTS; i++) {
        const RaidEnemy* E = &g_raidLevel.enemy[i];
        if (E->type > RAID_ENEMY_MAX_ID) continue;

        Entity* e = &g_EnemiesList[placed];
        memset(e, 0, sizeof(*e));

        e->status_flags = 1;                 // alive, drawn, updated
        e->behavior_flags = 0;
        *(unsigned short*)&e->angle = (unsigned short)(E->angle & 0xFFF);

        e->scaMatrixData.localMatrix.t[0] = (int)E->x;
        e->scaMatrixData.localMatrix.t[1] = 0;
        e->scaMatrixData.localMatrix.t[2] = (int)E->z;
        e->position.x = E->x;
        e->position.y = 0;
        e->position.z = E->z;
        e->position.pad = 0;                 // the rotation SVECTOR's X

        e->animationId = 0;
        e->animation_frame_id = 0;
        e->timing_control = 1;

        e->state = 0;
        e->ignore_player_flag = 0;
        e->action_behavior = 0;
        e->action_state = 0;
        e->id = E->type;
        e->death_event_id = 0xFF;            // no script event to raise on death
        e->hit_state = 0;
        e->collisionFlags = 0;
        e->lookAtFlags = 0;

        // The VALUE, not the table's address. See the header comment.
        e->Sca_info = g_scaDataTable[0];
        e->pSca_hit_data = g_scaPoolPtr;
        g_scaPoolPtr += RAID_ENEMY_SCA_PARTS * 6;

        placed++;
    }

    g_enemy_count = placed;
    if (placed == 0) return;

    // ---- the models, as room_set loads them --------------------------------
    //
    // g_LastEnemyModelId is room_set's own one-entry cache: consecutive
    // entities of the same kind share the loaded model instead of reading the
    // file again. Levels tend to list enemies in runs, so it earns its keep.
    g_LastEnemyModelId = 0xff;
    Entity* prev = NULL;

    for (int i = 0; i < placed; i++) {
        Entity* e = &g_EnemiesList[i];
        ENTITY = e;                          // every loader below reads the global

        if (prev != NULL && g_LastEnemyModelId == e->id) {
            e->animHeader = prev->animHeader;
            e->animBase = prev->animBase;
            e->modelLoadBuffer = prev->modelLoadBuffer - 0xc;
        } else {
            g_LastEnemyModelId = e->id;
            LoadEntityEMD(e, (unsigned char)(e->id + 4));
        }

        Entity_SetJoints(e, 0x7c);
        InitAnimStructure((void*)e->modelLoadBuffer);
        g_loadDataDestPointer = (void*)SetupJointStructures((unsigned int)g_loadDataDestPointer);
        prev = e;
    }

    ENTITY = saveEntity;
    s_spawned = 1;
}
