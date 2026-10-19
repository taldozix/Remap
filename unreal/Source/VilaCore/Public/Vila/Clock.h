// Tempo do jogo. 1 segundo real = `time_scale` minutos de jogo (um dia dura 24 min).
#pragma once
#include "Vila/Core.h"

namespace vila {

class Clock {
public:
	Signal<double, bool> time_advanced;  // (minutos, salto)
	Signal<int> hour_changed;
	Signal<int> day_changed;

	double total_minutes = 0.0;
	double time_scale = 1.0;
	bool running = false;

	// Chamado a cada quadro pelo jogo (segundos reais).
	void process(double delta_seconds);
	void hold() { ++holds_; }
	void release() { holds_ = std::max(0, holds_ - 1); }
	bool is_held() const { return holds_ > 0; }
	void reset_holds() { holds_ = 0; }
	// Salto de tempo em fatias de até 30 min (dormir, esperar).
	void advance(double minutes);
	void set_time(int day, int hour, int minute = 0);
	int day() const { return static_cast<int>(total_minutes / 1440.0) + 1; }
	double hour_f() const { return std::fmod(total_minutes, 1440.0) / 60.0; }
	int hour() const { return static_cast<int>(hour_f()); }
	int minute() const { return static_cast<int>(std::fmod(total_minutes, 60.0)); }
	int weekday() const { return (day() - 1) % 7; }  // 0 = segunda
	std::string weekday_name() const;
	std::string time_string() const;
	bool hour_in(double from_h, double to_h) const { return is_hour_in(hour_f(), from_h, to_h); }
	static bool is_hour_in(double h, double from_h, double to_h);
	bool is_night() const;
	Json to_dict() const { return Json{{"total_minutes", total_minutes}}; }
	void from_dict(const Json& d) { total_minutes = jnum(d, "total_minutes", 0.0); }

private:
	void step(double minutes, bool skipped);
	int holds_ = 0;
};

Clock& clock();

}  // namespace vila
