/*
 * Quasimodo RTX — optional JoltPhysics wrapper (J1 skeleton).
 * No Jolt types in this header. Implementations live in physics_jolt.cpp (ON) or physics_jolt_stub.cpp (OFF).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

int  PhysicsJolt_Init(void);
void PhysicsJolt_Shutdown(void);
int  PhysicsJolt_IsAvailable(void);
void PhysicsJolt_PrintInfo(void);

#ifdef __cplusplus
}
#endif
