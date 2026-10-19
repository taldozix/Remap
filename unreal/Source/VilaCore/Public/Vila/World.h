// Interface que o mundo 3D (Unreal: AVilaGameWorld) implementa para responder consultas
// espaciais da simulação. No Godot era o nó GameWorld chamado por has_method/call.
#pragma once
#include "Vila/Core.h"

namespace vila {

class VILACORE_API IWorld {
public:
	virtual ~IWorld() = default;
	virtual bool player_outdoors() const { return true; }
	virtual bool is_player_in_job_area() const { return true; }
	virtual void on_player_fainted() {}
	virtual void on_shift_ended(const Json& /*summary*/) {}
	virtual std::vector<std::string> find_witnesses(const Vec3& /*pos*/, double /*radius*/) const { return {}; }
	virtual bool is_vehicle_near_spot(const std::string& /*vehicle*/, const std::string& /*spot*/, double /*radius*/) const { return false; }
	virtual void refresh_vehicle(const std::string& /*vehicle*/) {}
	virtual void sync_to_state() {}
	virtual std::string save_block_reason() const { return ""; }
	// Eventos do diretor (feira, blitz...). Objeto vazio = o mundo não iniciou o evento.
	virtual Json start_event(const std::string& /*id*/, const Json& /*def*/) { return Json::object(); }
	virtual void end_event(const std::string& /*id*/, const Json& /*info*/) {}
};

}  // namespace vila
