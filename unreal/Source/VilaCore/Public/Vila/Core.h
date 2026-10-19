// Vila Aurora — núcleo de simulação em C++ puro (sem dependência do Unreal).
// Portado do protótipo em Godot (src/systems, src/autoload). O módulo do jogo no Unreal
// (Source/VilaAurora) usa este núcleo; aqui ele também compila e é testado com g++/CMake.
//
// Convenções (ver Source/VilaCore/CONVENTIONS.md):
//  - Dicionários/arrays dinâmicos do GDScript viram `Json` (nlohmann::ordered_json, mantém
//    a ordem de inserção como o Dictionary do Godot).
//  - Nomes de classes, métodos e campos são os mesmos do GDScript (snake_case), para que o
//    port seja verificável linha a linha.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "ThirdParty/nlohmann/json.hpp"

// No Unreal, o UBT define VILACORE_API (exportação da DLL do módulo). Fora dele, vazio.
#ifndef VILACORE_API
#define VILACORE_API
#endif

namespace vila {

using Json = nlohmann::ordered_json;

// ---------------------------------------------------------------- vetor
struct VILACORE_API Vec3 {
	double x = 0.0, y = 0.0, z = 0.0;
	Vec3() = default;
	Vec3(double px, double py, double pz) : x(px), y(py), z(pz) {}
	Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
	Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
	Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
	double length() const { return std::sqrt(x * x + y * y + z * z); }
	double distance_to(const Vec3& o) const { return (*this - o).length(); }
	bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
	Json to_json() const { return Json::array({x, y, z}); }
	static Vec3 from_json(const Json& j, const Vec3& def);
	static Vec3 from_json(const Json& j) { return from_json(j, Vec3()); }
};

// ---------------------------------------------------------------- sinais
// Equivalente aos `signal` do Godot: lista de ouvintes chamados em ordem.
template <typename... Args>
class Signal {
public:
	using Fn = std::function<void(Args...)>;
	int connect(Fn fn) {
		const int id = ++next_id_;
		slots_.push_back({id, std::move(fn)});
		return id;
	}
	void disconnect(int id) {
		slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [id](const Slot& s) { return s.id == id; }), slots_.end());
	}
	void emit(Args... args) const {
		// cópia: um ouvinte pode conectar/desconectar durante a emissão
		const auto copy = slots_;
		for (const auto& s : copy) s.fn(args...);
	}
	void clear() { slots_.clear(); }
	size_t size() const { return slots_.size(); }

private:
	struct Slot {
		int id;
		Fn fn;
	};
	std::vector<Slot> slots_;
	int next_id_ = 0;
};

// ---------------------------------------------------------------- acesso estilo GDScript
// `d.get("k", padrão)` do GDScript. Funcionam com qualquer Json (se não for objeto, devolvem o padrão).
VILACORE_API const Json& empty_json();                      // null
VILACORE_API const Json& jget(const Json& j, const std::string& key);  // null se ausente
VILACORE_API bool has(const Json& j, const std::string& key);
VILACORE_API std::string to_str(const Json& v);             // str(v) do GDScript (números sem ".0" se inteiros)
VILACORE_API double to_num(const Json& v, double def = 0.0);   // float(v)
VILACORE_API int to_int(const Json& v, int def = 0);           // int(v) (trunca)
VILACORE_API bool to_bool(const Json& v, bool def = false);    // bool(v)
VILACORE_API bool truthy(const Json& v);                    // regra de verdade do GDScript
VILACORE_API std::string jstr(const Json& j, const std::string& key, const std::string& def = "");
VILACORE_API double jnum(const Json& j, const std::string& key, double def = 0.0);
VILACORE_API int jint(const Json& j, const std::string& key, int def = 0);
VILACORE_API bool jbool(const Json& j, const std::string& key, bool def = false);
VILACORE_API const Json& jobj(const Json& j, const std::string& key);  // objeto vazio se ausente/não-objeto
VILACORE_API const Json& jarr(const Json& j, const std::string& key);  // array vazio se ausente/não-array
VILACORE_API bool json_array_has(const Json& arr, const Json& v);

// ---------------------------------------------------------------- matemática
inline double clampf(double v, double a, double b) { return std::max(a, std::min(b, v)); }
inline int clampi(int v, int a, int b) { return std::max(a, std::min(b, v)); }
inline double lerpf(double a, double b, double t) { return a + (b - a) * t; }
inline double move_toward(double from, double to, double delta) {
	if (std::abs(to - from) <= delta) return to;
	return from + (to > from ? delta : -delta);
}
inline double fposmod(double a, double b) {
	double r = std::fmod(a, b);
	return r < 0 ? r + b : r;
}
constexpr double TAU = 6.283185307179586;

// ---------------------------------------------------------------- texto
// Formatação estilo `"%s tem %d" % [a, b]` do GDScript (suporta %s %d %f %.Nf %0Nd %%).
struct VILACORE_API FmtArg {
	enum Kind { Str, Num } kind;
	std::string s;
	double n = 0.0;
	FmtArg(const std::string& v) : kind(Str), s(v) {}
	FmtArg(const char* v) : kind(Str), s(v) {}
	FmtArg(int v) : kind(Num), n(v) {}
	FmtArg(long v) : kind(Num), n(static_cast<double>(v)) {}
	FmtArg(long long v) : kind(Num), n(static_cast<double>(v)) {}
	FmtArg(unsigned v) : kind(Num), n(v) {}
	FmtArg(double v) : kind(Num), n(v) {}
	FmtArg(float v) : kind(Num), n(v) {}
	FmtArg(const Json& v);
};
VILACORE_API std::string fmt(const std::string& pattern, std::initializer_list<FmtArg> args);
VILACORE_API std::string join(const std::vector<std::string>& parts, const std::string& sep);
VILACORE_API std::string strip(const std::string& s);
VILACORE_API std::string lower(const std::string& s);
VILACORE_API std::string replace_all(std::string s, const std::string& from, const std::string& to);
VILACORE_API std::string capitalize_first(const std::string& s);

// Tradução: chaves são o texto em português (como no Godot). Idioma padrão pt_BR.
VILACORE_API void load_translations(const std::string& locale, const Json& table);
VILACORE_API void set_locale(const std::string& locale);
VILACORE_API const std::string& locale();
VILACORE_API std::string tr(const std::string& text);

// ---------------------------------------------------------------- aleatório
// Mesma interface do RandomNumberGenerator do Godot.
class VILACORE_API Rng {
public:
	Rng() { randomize(); }
	explicit Rng(uint64_t s) { seed(s); }
	void seed(uint64_t s) { eng_.seed(s); }
	void randomize();
	double randf();                               // [0, 1)
	double randf_range(double a, double b);       // [a, b)
	uint32_t randi();
	int randi_range(int a, int b);                // [a, b]
	template <typename T>
	const T& pick(const std::vector<T>& v) { return v[static_cast<size_t>(randi_range(0, static_cast<int>(v.size()) - 1))]; }
	const Json& pick(const Json& arr) { return arr[static_cast<size_t>(randi_range(0, static_cast<int>(arr.size()) - 1))]; }

private:
	std::mt19937_64 eng_;
};

// ---------------------------------------------------------------- arquivos
VILACORE_API bool read_text_file(const std::string& path, std::string& out);
VILACORE_API bool write_text_file(const std::string& path, const std::string& text);
VILACORE_API bool file_exists(const std::string& path);
VILACORE_API bool make_dirs(const std::string& path);
VILACORE_API bool remove_file(const std::string& path);
VILACORE_API bool copy_file(const std::string& from, const std::string& to);
VILACORE_API bool rename_file(const std::string& from, const std::string& to);
VILACORE_API double unix_time();
VILACORE_API std::string datetime_string();

// Avisos (push_warning do Godot). O Unreal pode redirecionar para UE_LOG.
extern VILACORE_API std::function<void(const std::string&)> warning_sink;
VILACORE_API void push_warning(const std::string& msg);

}  // namespace vila
