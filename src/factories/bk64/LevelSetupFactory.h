#pragma once

#include <factories/BaseFactory.h>
#include <vector>

namespace BK64 {

/**
 * NodeProp: Spawn point, warp, trigger, or event marker
 * 
 * Runtime Purpose:
 * - Defines spatial triggers and spawn locations for actors, warps, and events
 * - Processed during level load (func_8033F5A0_loadLevel) to create ActorMarkers
 * - Category (bit6) determines behavior:
 *   * 6 = Actor spawn point (creates dynamic entity via markerActorTypeArray dispatch)
 *   * 7 = Warp destination (teleports player to different map)
 *   * 9 = Trigger zone (activates events when player enters radius)
 *   * 0xA = Special event marker (used by level-specific systems)
 * 
 * Data Flow:
 *   ROM → LevelSetupFactory parse() → NodeProp array → func_8033F5A0_loadLevel →
 *   ActorMarker creation → Actor spawning/event triggering via overlay callbacks
 * 
 * Bitfield Packing (20 bytes total):
 *   Offset 0x00: x, y, z positions (3 × int16_t)
 *   Offset 0x06: radius:9, bit6:6, bit0:1 (uint16_t)
 *   Offset 0x08: Actor/Warp/Event ID (uint16_t)
 *   Offset 0x0A: Marker ID (uint8_t), padding (uint8_t)
 *   Offset 0x0C: yaw:9, scale:23 (uint32_t)
 *   Offset 0x10: Two 12-bit IDs, initialization flags, function parameters (uint32_t)
 */
typedef struct NodeProp {
    int16_t x, y, z;        // World position (s16 coordinates)
    uint16_t radius: 9;     // Trigger radius (distance check threshold)
    uint16_t bit6: 6;       // Category: 6=actor, 7=warp, 9=trigger, 0xA=event
    uint16_t bit0: 1;       // Active/enabled flag
    uint16_t unk8;          // Actor ID (bit6==6), Warp ID (bit6==7), or Event ID
    uint8_t unkA;           // ActorMarker ID for lookup
    uint8_t padB;           // Padding
    uint32_t yaw: 9;        // Spawn rotation (*2 for degrees, 0-511 → 0-1022°)
    uint32_t scale: 23;     // Spawn scale (fixed point, /1000.0 for actual scale)
    uint32_t unk10_31: 12;  // Secondary ID or overlay-specific parameter
    uint32_t unk10_19: 12;  // Additional parameters (animation phase, variant, etc.)
    uint32_t pad10_7: 1;    // Padding bit
    uint32_t unk10_6: 1;    // Initialized flag (set during runtime processing)
    uint32_t pad10_5: 4;    // Padding bits
    uint32_t unk10_0: 2;    // Function parameter (used in func_803303B8)
} NodeProp;

/**
 * ModelProp: Static 3D model (is_actor=0, is_3d=1) - 12 bytes
 * Asset ID = (unk0 & 0xFFF) + 0x2D1
 * Yaw/Roll *2 for degrees, Scale /100.0 for actual scale
 */
typedef struct ModelProp {
    uint16_t unk0;          // model_index:12, pad:4
    uint8_t yaw;
    uint8_t roll;
    int16_t position[3];
    uint8_t scale;
    uint8_t flags;
} ModelProp;

/**
 * SpriteProp: 2D billboard sprite (is_actor=0, is_3d=0) - 12 bytes
 * Asset ID = (word0 & 0xFFF) + 0x572
 * RGB at bits 13-21 (r:13-15, b:16-18, g:19-21), Scale /100.0
 */
typedef struct SpriteProp {
    uint32_t word0;         // sprite_index:12, unk0_19:1, r:3, b:3, g:3, scale:8, mirrored:1
    int16_t unk4[3];
    uint16_t word8;         // frame:5, unk8_10:5, flags:6
} SpriteProp;

/**
 * ActorProp: Dynamic entity created at runtime (is_actor=1) - 12 bytes
 * Created by func_8032F21C when actors spawn from NodeProp definitions.
 * ROM contains only ModelProp and SpriteProp.
 */
typedef struct ActorProp {
    uint32_t marker;        // ActorMarker* (runtime only)
    int16_t x, y, z;        // Position cache
    uint16_t flags;
} ActorProp;

/**
 * Prop: Union of ModelProp, SpriteProp, ActorProp (12 bytes)
 * 
 * Type discrimination (flags at offset 0xA):
 *   is_actor=1 → ActorProp (runtime only)
 *   is_actor=0, is_3d=1 → ModelProp
 *   is_actor=0, is_3d=0 → SpriteProp
 */
typedef union Prop {
    ModelProp model;
    SpriteProp sprite;
    ActorProp actor;
    struct {
        uint32_t pad0;
        int16_t unk4[3];
        uint16_t pad8_15: 10;
        uint16_t unk8_5: 1;
        uint16_t unk8_4: 1;
        uint16_t unk8_3: 1;
        uint16_t unk8_2: 1;
        uint16_t is_3d: 1;
        uint16_t is_actor: 1;
    };
    uint8_t raw[12];
} Prop;

/**
 * CubeData: Spatial partition cell in 32×32×32 grid
 * 
 * Runtime Purpose:
 * - Implements spatial partitioning for efficient collision detection and rendering
 * - Each cube covers ~500-800 world units per axis (varies by level)
 * - Active cubes determined by player/camera position (func_80336934_getActiveCubes)
 * - Only entities in active cubes are processed (rendering, collision, AI update)
 * 
 * Coordinate System:
 * - Grid coordinates (x, y, z) are 5-bit values: range 0-31
 * - World coordinates converted via: cubeCoord = (worldCoord - levelOrigin) / cubeSize
 * - Example: Treasure Trove Cove uses ~700 units per cube
 * 
 * unk0_4 Boundary:
 * - Separates regular NodeProps from event markers
 * - NodeProps [0..unk0_4): Regular spawns/warps processed during level load
 * - NodeProps [unk0_4..prop1Cnt): Events processed by level-specific overlays
 * - Used by tutorial system (Spiral Mountain), minigames (Furnace Fun), puzzles
 * 
 * Usage Examples:
 * - Collision: func_80336BC4_getCubeForPosition → check Props for collision geometry
 * - Rendering: func_80337048_renderCubeModels → render ModelProps in visible cubes
 * - Actor Spawning: func_80336C58_processActorPropsInCubes → spawn actors from NodeProps
 * - Event Triggers: func_80336D70_checkEventTriggers → check NodeProp radius distance
 * 
 * Bitfield Packing (header 32 bits):
 *   bits 0-4: x coordinate (5 bits, 0-31)
 *   bits 5-9: y coordinate (5 bits, 0-31)
 *   bits 10-14: z coordinate (5 bits, 0-31)
 *   bits 15-20: prop1Cnt (6 bits, max 63 NodeProps)
 *   bits 21-26: prop2Cnt (6 bits, max 63 Props)
 *   bits 27-31: unk0_4 (5 bits, boundary index)
 */
typedef struct CubeData {
    int32_t x: 5;           // X coordinate in grid (0-31)
    int32_t y: 5;           // Y coordinate in grid (0-31)
    int32_t z: 5;           // Z coordinate in grid (0-31)
    uint32_t prop1Cnt: 6;   // Count of NodeProps (max 63)
    uint32_t prop2Cnt: 6;   // Count of Props (max 63)
    uint32_t unk0_4: 5;     // Boundary: [0..unk0_4) spawns, [unk0_4..prop1Cnt) events
    std::vector<NodeProp> nodeProps;  // Spawn points, warps, triggers (prop1Ptr in runtime)
    std::vector<Prop> props;          // Visual instances: models/sprites (prop2Ptr in runtime)
} CubeData;

class LevelSetupData : public IParsedData {
  public:
    std::vector<CubeData> mCubes;

    LevelSetupData(std::vector<CubeData> cubes) : mCubes(std::move(cubes)) {}
};

class LevelSetupHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelSetupBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelSetupCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelSetupModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class LevelSetupFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { 
            REGISTER(Code, LevelSetupCodeExporter) 
            REGISTER(Header, LevelSetupHeaderExporter)                     
            REGISTER(Binary, LevelSetupBinaryExporter)
            REGISTER(Modding, LevelSetupModdingExporter)
        };
    }
    
    bool HasModdedDependencies() override { return true; }
};
} // namespace BK64
