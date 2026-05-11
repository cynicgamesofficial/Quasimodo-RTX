/*
 * Quasimodo RTX — Jolt minimal library init when QUASIMODO_JOLT_PHYSICS is ON.
 * J1 only: RegisterTypes / Factory — no PhysicsSystem, no bodies, no engine hooks.
 *
 * Do not include Quake headers here — MSVC + Jolt + shared.h can clash on macros/CRT order.
 * Do not use JPH_SUPPRESS_WARNINGS before <cstdarg>/<cstdio> here — MSVC then fails parsing UCRT float.h.
 */

#include <cstdarg>
#include <cstdio>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include "physics_jolt.h"

static bool g_jolt_core_ready = false;

static void PhysicsJolt_Trace(const char *inFMT, ...)
{
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);
	std::fputs(buffer, stderr);
}

#ifdef JPH_ENABLE_ASSERTS
static bool PhysicsJolt_AssertFailed(const char *inExpression, const char *inMessage, const char *inFile, JPH::uint inLine)
{
	std::fprintf(stderr, "%s:%u: (%s) %s\n", inFile, (unsigned)inLine, inExpression, inMessage ? inMessage : "");
	return true;
}
#endif

int PhysicsJolt_Init(void)
{
	if (g_jolt_core_ready)
		return 1;

	JPH::RegisterDefaultAllocator();
	JPH::Trace = PhysicsJolt_Trace;
#ifdef JPH_ENABLE_ASSERTS
	JPH::AssertFailed = PhysicsJolt_AssertFailed;
#endif

	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();

	g_jolt_core_ready = true;
	return 1;
}

void PhysicsJolt_Shutdown(void)
{
	if (!g_jolt_core_ready)
		return;

	JPH::UnregisterTypes();
	delete JPH::Factory::sInstance;
	JPH::Factory::sInstance = nullptr;

	g_jolt_core_ready = false;
}

int PhysicsJolt_IsAvailable(void)
{
	return g_jolt_core_ready ? 1 : 0;
}

void PhysicsJolt_PrintInfo(void)
{
	if (g_jolt_core_ready)
		std::fprintf(stderr, "[JoltPhysics] core registered (J1 skeleton; no simulation).\n");
	else
		std::fprintf(stderr, "[JoltPhysics] not initialized.\n");
}
