#pragma once

#include <array>
#include <cstdint>

namespace ysmir::runtime
{
	// Steam 1.7.104.0, verified against the executable recorded in BUILD-REPORT.json.
	inline constexpr std::uintptr_t MainUpdateRva = 0x658870;
	inline constexpr std::uintptr_t FrameCallOffset = 0xC33;
	inline constexpr std::array<std::uint8_t, 5> FrameCallBytes{ 0xE8, 0x98, 0xCF, 0xCE, 0xFF };
	inline constexpr std::uintptr_t VoiceVTableRva = 0x1937F08;
	inline constexpr std::uintptr_t ProjectileVTableRva = 0x1942A10;
	inline constexpr std::uintptr_t BeamVTableRva = 0x193D630;
	inline constexpr std::uintptr_t MissileVTableRva = 0x1940E58;
	inline constexpr std::uintptr_t ConeVTableRva = 0x193DEA0;
}
