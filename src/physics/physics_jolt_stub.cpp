/*
 * Quasimodo RTX — Jolt stub when QUASIMODO_JOLT_PHYSICS is OFF at configure time.
 */

#include "physics_jolt.h"

int PhysicsJolt_Init(void)
{
	return 0;
}

void PhysicsJolt_Shutdown(void)
{
}

int PhysicsJolt_IsAvailable(void)
{
	return 0;
}

void PhysicsJolt_PrintInfo(void)
{
}
