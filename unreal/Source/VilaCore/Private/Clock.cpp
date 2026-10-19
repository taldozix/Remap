#include "Vila/Clock.h"

namespace vila {

Clock& clock() {
	static Clock c;
	return c;
}

static const char* WEEKDAYS[] = {"Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado", "Domingo"};

void Clock::process(double delta_seconds) {
	if (!running || holds_ > 0) return;
	step(delta_seconds * time_scale, false);
}

void Clock::advance(double minutes) {
	double left = minutes;
	while (left > 0.0) {
		const double s = std::min(30.0, left);
		step(s, true);
		left -= s;
	}
}

void Clock::step(double minutes, bool skipped) {
	const int old_hour = static_cast<int>(total_minutes / 60.0);
	const int old_day = day();
	total_minutes += minutes;
	time_advanced.emit(minutes, skipped);
	if (static_cast<int>(total_minutes / 60.0) != old_hour) hour_changed.emit(hour());
	if (day() != old_day) day_changed.emit(day());
}

void Clock::set_time(int d, int h, int m) { total_minutes = (d - 1) * 1440.0 + h * 60.0 + m; }

std::string Clock::weekday_name() const { return tr(WEEKDAYS[weekday()]); }

std::string Clock::time_string() const { return fmt("%02d:%02d", {hour(), minute()}); }

bool Clock::is_hour_in(double h, double from_h, double to_h) {
	if (from_h <= to_h) return h >= from_h && h < to_h;
	return h >= from_h || h < to_h;
}

bool Clock::is_night() const {
	const double h = hour_f();
	return h < 6.0 || h >= 19.0;
}

}  // namespace vila
