#include "app/shell_scene.hpp"

#include "assets/kit_scene.hpp"
#include "net/portal_server.hpp"

#include <string>

namespace sc::app {
namespace {

void append_piece(std::string& json, const KitPlacement& placement, const KitPiece& piece,
                  bool& first) {
    if (!first) {
        json += ',';
    }
    first = false;
    json += "{\"x\":";
    json += std::to_string(placement.x + piece.x);
    json += ",\"y\":";
    json += std::to_string(piece.y + 1);  // kits stand one voxel above the floor
    json += ",\"z\":";
    json += std::to_string(placement.z + piece.z);
    json += ",\"w\":";
    json += std::to_string(piece.width);
    json += ",\"h\":";
    json += std::to_string(piece.height);
    json += ",\"d\":";
    json += std::to_string(piece.depth);
    json += ",\"m\":";
    json += std::to_string(static_cast<unsigned>(piece.material));
    json += '}';
}

}  // namespace

std::string factory_scene_json() {
    std::string json;
    json.reserve(32768);
    json += R"({"kits":[)";

    bool first_kit = true;
    for (const KitPlacement& placement : kFactoryKitLayout) {
        if (!first_kit) {
            json += ',';
        }
        first_kit = false;

        const Kit kit = generate_machine_kit(placement.kind, placement.seed);
        json += "{\"id\":\"";
        json += net::json_escape(kit.name);
        json += "\",\"label\":\"";
        json += net::json_escape(kit.name);
        json += "\",\"x\":";
        json += std::to_string(placement.x);
        json += ",\"z\":";
        json += std::to_string(placement.z);
        json += ",\"pieces\":[";

        bool first_piece = true;
        for (const KitPiece& piece : kit.pieces) {
            append_piece(json, placement, piece, first_piece);
        }
        json += "]}";
    }

    json += "]}";
    return json;
}

}  // namespace sc::app
