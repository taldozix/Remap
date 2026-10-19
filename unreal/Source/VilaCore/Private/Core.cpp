#include "Vila/Core.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace vila {

namespace fs = std::filesystem;

Vec3 Vec3::from_json(const Json& j, const Vec3& def) {
	if (!j.is_array() || j.size() < 2) return def;
	if (j.size() == 2) return {to_num(j[0]), 0.0, to_num(j[1])};
	return {to_num(j[0]), to_num(j[1]), to_num(j[2])};
}

// ---------------------------------------------------------------- Json
const Json& empty_json() {
	static const Json e;
	return e;
}

static const Json& empty_object() {
	static const Json e = Json::object();
	return e;
}

static const Json& empty_array() {
	static const Json e = Json::array();
	return e;
}

const Json& jget(const Json& j, const std::string& key) {
	if (!j.is_object()) return empty_json();
	auto it = j.find(key);
	return it == j.end() ? empty_json() : *it;
}

bool has(const Json& j, const std::string& key) { return j.is_object() && j.contains(key); }

static std::string num_to_str(double d) {
	if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1e15) {
		// Godot imprime floats inteiros como "3.0"; para ids/textos o formato inteiro é o útil.
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.0f", d);
		return buf;
	}
	std::ostringstream ss;
	ss.precision(14);
	ss << d;
	return ss.str();
}

std::string to_str(const Json& v) {
	switch (v.type()) {
		case Json::value_t::string: return v.get<std::string>();
		case Json::value_t::number_integer: return std::to_string(v.get<long long>());
		case Json::value_t::number_unsigned: return std::to_string(v.get<unsigned long long>());
		case Json::value_t::number_float: return num_to_str(v.get<double>());
		case Json::value_t::boolean: return v.get<bool>() ? "true" : "false";
		case Json::value_t::null: return "<null>";
		default: return v.dump();
	}
}

double to_num(const Json& v, double def) {
	switch (v.type()) {
		case Json::value_t::number_integer:
		case Json::value_t::number_unsigned:
		case Json::value_t::number_float: return v.get<double>();
		case Json::value_t::boolean: return v.get<bool>() ? 1.0 : 0.0;
		case Json::value_t::string: {
			const std::string& s = v.get_ref<const std::string&>();
			char* end = nullptr;
			double d = std::strtod(s.c_str(), &end);
			return end == s.c_str() ? 0.0 : d;
		}
		default: return def;
	}
}

int to_int(const Json& v, int def) {
	if (v.is_null() || v.is_object() || v.is_array()) return def;
	return static_cast<int>(to_num(v, def));
}

bool to_bool(const Json& v, bool def) {
	if (v.is_null()) return def;
	return truthy(v);
}

bool truthy(const Json& v) {
	switch (v.type()) {
		case Json::value_t::null: return false;
		case Json::value_t::boolean: return v.get<bool>();
		case Json::value_t::number_integer:
		case Json::value_t::number_unsigned:
		case Json::value_t::number_float: return v.get<double>() != 0.0;
		case Json::value_t::string: return !v.get_ref<const std::string&>().empty();
		case Json::value_t::object:
		case Json::value_t::array: return !v.empty();
		default: return false;
	}
}

std::string jstr(const Json& j, const std::string& key, const std::string& def) {
	const Json& v = jget(j, key);
	return v.is_null() ? def : to_str(v);
}
double jnum(const Json& j, const std::string& key, double def) {
	const Json& v = jget(j, key);
	return v.is_null() ? def : to_num(v, def);
}
int jint(const Json& j, const std::string& key, int def) {
	const Json& v = jget(j, key);
	return v.is_null() ? def : to_int(v, def);
}
bool jbool(const Json& j, const std::string& key, bool def) {
	const Json& v = jget(j, key);
	return v.is_null() ? def : truthy(v);
}
const Json& jobj(const Json& j, const std::string& key) {
	const Json& v = jget(j, key);
	return v.is_object() ? v : empty_object();
}
const Json& jarr(const Json& j, const std::string& key) {
	const Json& v = jget(j, key);
	return v.is_array() ? v : empty_array();
}
bool json_array_has(const Json& arr, const Json& v) {
	if (!arr.is_array()) return false;
	for (const auto& e : arr)
		if (e == v) return true;
	return false;
}

// ---------------------------------------------------------------- texto
FmtArg::FmtArg(const Json& v) {
	if (v.is_number() || v.is_boolean()) {
		kind = Num;
		n = to_num(v);
	} else {
		kind = Str;
		s = to_str(v);
	}
}

std::string fmt(const std::string& pattern, std::initializer_list<FmtArg> args) {
	std::string out;
	auto it = args.begin();
	for (size_t i = 0; i < pattern.size(); ++i) {
		char c = pattern[i];
		if (c != '%') {
			out += c;
			continue;
		}
		if (i + 1 < pattern.size() && pattern[i + 1] == '%') {
			out += '%';
			++i;
			continue;
		}
		// especificador: %[0][largura][.precisão](s|d|f)
		size_t j = i + 1;
		std::string spec = "%";
		while (j < pattern.size() && (std::isdigit(static_cast<unsigned char>(pattern[j])) || pattern[j] == '.' || pattern[j] == '-' || pattern[j] == '+')) spec += pattern[j++];
		if (j >= pattern.size()) {
			out += pattern.substr(i);
			break;
		}
		char conv = pattern[j];
		if (it == args.end()) {
			out += spec + conv;
			i = j;
			continue;
		}
		const FmtArg& a = *it++;
		char buf[256];
		if (conv == 's') {
			std::string sv = a.kind == FmtArg::Str ? a.s : num_to_str(a.n);
			std::snprintf(buf, sizeof(buf), (spec + "s").c_str(), sv.c_str());
			out += buf;
		} else if (conv == 'd' || conv == 'i') {
			long long n = a.kind == FmtArg::Num ? static_cast<long long>(a.n) : std::atoll(a.s.c_str());
			std::snprintf(buf, sizeof(buf), (spec + "lld").c_str(), n);
			out += buf;
		} else if (conv == 'f') {
			double n = a.kind == FmtArg::Num ? a.n : std::atof(a.s.c_str());
			std::snprintf(buf, sizeof(buf), (spec + "f").c_str(), n);
			out += buf;
		} else {
			out += spec + conv;
		}
		i = j;
	}
	return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
	std::string out;
	for (size_t i = 0; i < parts.size(); ++i) {
		if (i) out += sep;
		out += parts[i];
	}
	return out;
}

std::string strip(const std::string& s) {
	size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return "";
	size_t b = s.find_last_not_of(" \t\r\n");
	return s.substr(a, b - a + 1);
}

std::string lower(const std::string& s) {
	std::string o = s;
	for (auto& ch : o) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	return o;
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
	if (from.empty()) return s;
	size_t pos = 0;
	while ((pos = s.find(from, pos)) != std::string::npos) {
		s.replace(pos, from.size(), to);
		pos += to.size();
	}
	return s;
}

std::string capitalize_first(const std::string& s) {
	if (s.empty()) return s;
	std::string o = s;
	o[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(o[0])));
	return o;
}

// ---------------------------------------------------------------- tradução
static std::map<std::string, Json>& translations() {
	static std::map<std::string, Json> t;
	return t;
}
static std::string& current_locale() {
	static std::string l = "pt_BR";
	return l;
}

void load_translations(const std::string& loc, const Json& table) { translations()[loc] = table; }
void set_locale(const std::string& loc) { current_locale() = loc; }
const std::string& locale() { return current_locale(); }

std::string tr(const std::string& text) {
	auto it = translations().find(current_locale());
	if (it == translations().end()) return text;
	const Json& v = jget(it->second, text);
	return v.is_string() ? v.get<std::string>() : text;
}

// ---------------------------------------------------------------- aleatório
void Rng::randomize() {
	std::random_device rd;
	eng_.seed((static_cast<uint64_t>(rd()) << 32) ^ rd() ^ static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
}
double Rng::randf() { return std::uniform_real_distribution<double>(0.0, 1.0)(eng_); }
double Rng::randf_range(double a, double b) { return a + (b - a) * randf(); }
uint32_t Rng::randi() { return static_cast<uint32_t>(eng_() & 0xffffffffu); }
int Rng::randi_range(int a, int b) {
	if (b < a) std::swap(a, b);
	return std::uniform_int_distribution<int>(a, b)(eng_);
}

// ---------------------------------------------------------------- arquivos
bool read_text_file(const std::string& path, std::string& out) {
	std::ifstream f(path, std::ios::binary);
	if (!f) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}
bool write_text_file(const std::string& path, const std::string& text) {
	std::ofstream f(path, std::ios::binary | std::ios::trunc);
	if (!f) return false;
	f << text;
	return static_cast<bool>(f);
}
bool file_exists(const std::string& path) {
	std::error_code ec;
	return fs::is_regular_file(path, ec);
}
bool make_dirs(const std::string& path) {
	std::error_code ec;
	fs::create_directories(path, ec);
	return !ec;
}
bool remove_file(const std::string& path) {
	std::error_code ec;
	return fs::remove(path, ec);
}
bool copy_file(const std::string& from, const std::string& to) {
	std::error_code ec;
	fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
	return !ec;
}
bool rename_file(const std::string& from, const std::string& to) {
	std::error_code ec;
	fs::rename(from, to, ec);
	return !ec;
}
double unix_time() {
	return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string datetime_string() {
	std::time_t t = std::time(nullptr);
	char buf[64];
	std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
	return buf;
}

std::function<void(const std::string&)> warning_sink;
void push_warning(const std::string& msg) {
	if (warning_sink)
		warning_sink(msg);
	else
		std::cerr << "AVISO: " << msg << std::endl;
}

}  // namespace vila
