extends Node
## Tempo do jogo. 1 segundo real = `time_scale` minutos de jogo (padrão: 1 → um dia dura 24 min).
## Emite `time_advanced` todo quadro (e em saltos, como dormir) para os sistemas de simulação.

signal time_advanced(minutes: float, skipped: bool)
signal hour_changed(hour: int)
signal day_changed(day: int)

const WEEKDAYS := ["Segunda", "Terça", "Quarta", "Quinta", "Sexta", "Sábado", "Domingo"]

var total_minutes: float = 0.0
var time_scale: float = 1.0
var running: bool = false
var _holds: int = 0  # diálogos etc. seguram o relógio

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_PAUSABLE

func _process(delta: float) -> void:
	if not running or _holds > 0:
		return
	_step(delta * time_scale, false)

func hold() -> void:
	_holds += 1

func release() -> void:
	_holds = max(0, _holds - 1)

func is_held() -> bool:
	return _holds > 0

func reset_holds() -> void:
	_holds = 0

## Avança o relógio em saltos (dormir, esperar). Divide em fatias de até 30 min
## para que necessidades e rotinas evoluam de forma coerente.
func advance(minutes: float) -> void:
	var left := minutes
	while left > 0.0:
		var step: float = min(30.0, left)
		_step(step, true)
		left -= step

func _step(minutes: float, skipped: bool) -> void:
	var old_hour := int(total_minutes / 60.0)
	var old_day := day()
	total_minutes += minutes
	time_advanced.emit(minutes, skipped)
	var new_hour := int(total_minutes / 60.0)
	if new_hour != old_hour:
		hour_changed.emit(hour())
	if day() != old_day:
		day_changed.emit(day())

func set_time(p_day: int, p_hour: int, p_minute: int = 0) -> void:
	total_minutes = (p_day - 1) * 1440.0 + p_hour * 60.0 + p_minute

func day() -> int:
	return int(total_minutes / 1440.0) + 1

func hour_f() -> float:
	return fmod(total_minutes, 1440.0) / 60.0

func hour() -> int:
	return int(hour_f())

func minute() -> int:
	return int(fmod(total_minutes, 60.0))

## 0 = segunda … 6 = domingo (dia 1 é uma segunda-feira).
func weekday() -> int:
	return (day() - 1) % 7

func weekday_name() -> String:
	return tr(WEEKDAYS[weekday()])

func time_string() -> String:
	return "%02d:%02d" % [hour(), minute()]

## Verdadeiro se a hora atual está no intervalo [from, to) — aceita intervalos que viram a noite.
func hour_in(from_h: float, to_h: float) -> bool:
	return Clock.is_hour_in(hour_f(), from_h, to_h)

static func is_hour_in(h: float, from_h: float, to_h: float) -> bool:
	if from_h <= to_h:
		return h >= from_h and h < to_h
	return h >= from_h or h < to_h

func is_night() -> bool:
	var h := hour_f()
	return h < 6.0 or h >= 19.0

func to_dict() -> Dictionary:
	return {"total_minutes": total_minutes}

func from_dict(d: Dictionary) -> void:
	total_minutes = float(d.get("total_minutes", 0.0))
