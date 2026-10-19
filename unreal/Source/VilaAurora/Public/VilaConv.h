// Conversões entre o espaço do núcleo/Godot e o do Unreal.
//
// Núcleo (vila::Vec3, JSON do mapa): metros, Y para cima, mão direita; mapas 2D são [x, z].
// Unreal: centímetros, Z para cima, mão esquerda, X para frente.
//   UE.X = x * 100   UE.Y = z * 100   UE.Z = y * 100
// Rotação: o Godot gira em Y (graus, anti-horário visto de cima) e a frente é -Z.
//   Yaw(UE) = -90 - rot_y(Godot)
#pragma once

#include "CoreMinimal.h"
#include "Vila/Core.h"

namespace VilaConv
{
	constexpr double M = 100.0;  // metros -> centímetros

	inline FVector ToUE(const vila::Vec3& P) { return FVector(P.x * M, P.z * M, P.y * M); }
	inline FVector ToUE(double X, double Y, double Z) { return FVector(X * M, Z * M, Y * M); }
	// Ponto do mapa 2D ([x, z] em metros) na altura `Y` (metros).
	inline FVector Map2D(double X, double Z, double Y = 0.0) { return FVector(X * M, Z * M, Y * M); }
	inline vila::Vec3 FromUE(const FVector& V) { return vila::Vec3(V.X / M, V.Z / M, V.Y / M); }

	// Lê [x, z] ou [x, y, z] (metros) do JSON. `Y` é usado quando o array tem 2 elementos.
	inline FVector JsonPos(const vila::Json& A, double Y = 0.0)
	{
		if (!A.is_array() || A.size() < 2) return FVector::ZeroVector;
		if (A.size() == 2) return Map2D(vila::to_num(A[0]), vila::to_num(A[1]), Y);
		return ToUE(vila::to_num(A[0]), vila::to_num(A[1]), vila::to_num(A[2]));
	}
	inline vila::Json PosJson(const FVector& V) { const vila::Vec3 P = FromUE(V); return vila::Json::array({P.x, P.y, P.z}); }

	// Tamanhos (metros, eixo x/z do Godot) -> vetor UE em cm.
	inline FVector Size(double SX, double SY, double SZ) { return FVector(SX * M, SZ * M, SY * M); }

	inline double YawFromGodot(double Deg) { return FRotator::NormalizeAxis(-90.0 - Deg); }
	inline double GodotYawFromUE(double Yaw) { return FRotator::NormalizeAxis(-90.0 - Yaw); }
	inline FRotator RotFromGodot(double Deg) { return FRotator(0.0, YawFromGodot(Deg), 0.0); }

	// Lado de parede do mapa ("n", "s", "e", "w"): normal para fora, no espaço UE.
	// Godot: n = -z, s = +z, e = +x, w = -x.
	inline FVector SideNormal(const std::string& Side)
	{
		if (Side == "n") return FVector(0, -1, 0);
		if (Side == "s") return FVector(0, 1, 0);
		if (Side == "e") return FVector(1, 0, 0);
		return FVector(-1, 0, 0);
	}

	// ------------------------------------------------------------ texto
	inline FString S(const std::string& Str) { return FString(UTF8_TO_TCHAR(Str.c_str())); }
	inline std::string U(const FString& Str) { return std::string(TCHAR_TO_UTF8(*Str)); }
	inline FText T(const std::string& Str) { return FText::FromString(S(Str)); }
	// Texto traduzido pelo núcleo (chave em português, como no Godot).
	inline FString Tr(const std::string& Key) { return S(vila::tr(Key)); }
	inline FText TrT(const std::string& Key) { return FText::FromString(Tr(Key)); }

	// "#rrggbb" (sRGB, como o Color() do Godot) -> linear.
	inline FLinearColor Hex(const std::string& HexStr, const FLinearColor& Default = FLinearColor::White)
	{
		if (HexStr.empty()) return Default;
		return FLinearColor::FromSRGBColor(FColor::FromHex(S(HexStr)));
	}
	inline FLinearColor JsonColor(const vila::Json& J, const std::string& Key, const FLinearColor& Default = FLinearColor::White)
	{
		const vila::Json& V = vila::jget(J, Key);
		return V.is_string() ? Hex(V.get<std::string>(), Default) : Default;
	}
}
