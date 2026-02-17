#pragma once

#include <factories/BaseFactory.h>
#include <vector>

namespace BK64 {

/**
 * NodeProp: Spawn point, warp, trigger, or event marker - 20 bytes
 * Decomp: NodeProp from props.h
 * 
 * Runtime Purpose:
 * - Defines spatial triggers and spawn locations for actors, warps, and events
 * - Processed during level load (func_8033F5A0_loadLevel) to create ActorMarkers
 * - category field determines behavior:
 *   * 6 = Actor spawn point (creates dynamic entity via markerActorTypeArray dispatch)
 *   * 7 = Warp destination (teleports player to different map)
 *   * 9 = Trigger zone (activates events when player enters radius)
 *   * 0xA = Special event marker (used by level-specific systems)
 * 
 * Data Flow:
 *   ROM → MapFactory parse() → NodeProp array → func_8033F5A0_loadLevel →
 *   ActorMarker creation → Actor spawning/event triggering via overlay callbacks
 * 
 * Structure Layout:
 *   Offset 0x00: position[3] (s16[3]) - X, Y, Z world coordinates
 *   Offset 0x06: selector_or_radius:9, category:6, bit0:1 (u16)
 *   Offset 0x08: actorId (u16) - Actor/Warp/Event ID depending on category
 *   Offset 0x0A: markerId (u8), padB (u8)
 *   Offset 0x0C: yaw:9, scale:23 (u32)
 *   Offset 0x10: unk10_31:12, unk10_19:12, pad10_7:1, unk10_6:1, pad10_5:4, unk10_0:2 (u32)
 */
typedef struct NodeProp {
    int16_t x, y, z;        // position[3]: World position (s16 coordinates)
    uint16_t radius: 9;     // selector_or_radius: Trigger radius for detection/volume
    uint16_t bit6: 6;       // category: Object type (6=actor, 7=warp, 9=trigger, 0xA=event)
    uint16_t bit0: 1;       // Active/enabled flag
    uint16_t unk8;          // actorId: Actor/Warp/Event ID (meaning depends on category)
    uint8_t unkA;           // markerId: ActorMarker ID for lookup table
    uint8_t padB;           // Padding byte
    uint32_t yaw: 9;        // Spawn rotation Y-axis (*2 for degrees, 0-511 → 0-1022°)
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
 * Decomp: model_prop_s from props.h
 * 
 * Structure Layout:
 *   Offset 0x00: unk0 (u16) - modelId:12, pad0_19:4
 *   Offset 0x02: yaw (u8) - rotation Y-axis
 *   Offset 0x03: roll (u8) - rotation around local axis
 *   Offset 0x04: position[3] (s16[3]) - X, Y, Z world position
 *   Offset 0x0A: scale (u8)
 *   Offset 0x0B: flags (u8) - isModelProp:1, isActorProp:1, etc.
 * 
 * Asset ID = (modelId & 0xFFF) + MODEL_ASSET_OFFSET (0x2D1)
 */
typedef struct ModelProp {
    uint16_t unk0;          // modelId:12, pad0_19:4
    uint8_t yaw;            // Y-axis rotation
    uint8_t roll;           // Roll rotation
    int16_t position[3];    // X, Y, Z world position
    uint8_t scale;          // Scale value
    uint8_t flags;          // Discriminator flags (isModelProp=1, isActorProp=0)
} ModelProp;

/**
 * SpriteProp: 2D billboard sprite (is_actor=0, is_3d=0) - 12 bytes  
 * Decomp: sprite_prop_s from props.h
 * 
 * Structure Layout (word0 - 32-bit big-endian):
 *   Bits 31-20: spriteId (12 bits) → Asset ID = spriteId + SPRITE_ASSET_OFFSET (0x572)
 *   Bit 19: unk0_19 (1 bit)
 *   Bits 18-16: rgb_remove_red (3 bits, 0-7 color removal value)
 *   Bits 15-13: rgb_remove_green (3 bits, 0-7 color removal value)
 *   Bits 12-10: rgb_remove_blue (3 bits, 0-7 color removal value)
 *   Bits 9-2: scale (8 bits)
 *   Bit 1: isMirrored (1 bit, horizontal flip)
 *   Bit 0: pad0_0 (1 bit)
 * 
 * Structure Layout (word8 - 16-bit big-endian at offset 0x0A):
 *   Bits 15-11: frame (5 bits, animation frame index)
 *   Bits 10-6: unk8_10 (5 bits)
 *   Bit 5: unk8_5 (1 bit)
 *   Bit 4: isNotFeatherEggOrNote (1 bit)
 *   Bit 3: unk8_3 (1 bit)
 *   Bit 2: isCollisionResolved (1 bit)
 *   Bit 1: isModelProp (1 bit, always 0 for sprites)
 *   Bit 0: isActorProp (1 bit, always 0 for sprites)
 */
typedef struct SpriteProp {
    uint32_t word0;         // Packed: spriteId:12, unk0_19:1, rgb_remove_red:3, rgb_remove_green:3, rgb_remove_blue:3, scale:8, isMirrored:1, pad0_0:1
    int16_t unk4[3];        // position[3]: X, Y, Z (offset 0x04-0x09)
    uint16_t word8;         // Packed: frame:5, unk8_10:5, flags:6
} SpriteProp;

/**
 * ActorProp: Dynamic entity created at runtime (is_actor=1) - 12 bytes
 * Decomp: actor_prop_s from props.h
 * 
 * Structure Layout:
 *   Offset 0x00: marker (ActorMarker* - 4 bytes, runtime pointer)
 *   Offset 0x04: position[3] (s16[3] - 6 bytes, X/Y/Z cache)
 *   Offset 0x0A: flags (u16 - 2 bytes) - frame:5, unk8_10:5, isMirrored:1, 
 *                isNotFeatherEggOrNote:1, unk8_3:1, isCollisionResolved:1, 
 *                isModelProp:1 (0), isActorProp:1 (1)
 * 
 * NOTE: ActorProps are created at runtime when actors spawn from NodeProp entries.
 *       ROM data contains ONLY ModelProp and SpriteProp - never ActorProp.
 */
typedef struct ActorProp {
    uint32_t marker;        // ActorMarker* pointer (runtime only)
    int16_t x, y, z;        // Position cache from position[3]
    uint16_t flags;         // Discriminator flags (isActorProp=1)
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

class MapData : public IParsedData {
  public:
    std::vector<CubeData> mCubes;

    MapData(std::vector<CubeData> cubes) : mCubes(std::move(cubes)) {}
};

class MapHeaderExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MapBinaryExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MapCodeExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MapModdingExporter : public BaseExporter {
    ExportResult Export(std::ostream& write, std::shared_ptr<IParsedData> data, std::string& entryName,
                        YAML::Node& node, std::string* replacement) override;
};

class MapFactory : public BaseFactory {
  public:
    std::optional<std::shared_ptr<IParsedData>> parse(std::vector<uint8_t>& buffer, YAML::Node& data) override;
    inline std::unordered_map<ExportType, std::shared_ptr<BaseExporter>> GetExporters() override {
        return { 
            REGISTER(Code, MapCodeExporter) 
            REGISTER(Header, MapHeaderExporter)                     
            REGISTER(Binary, MapBinaryExporter)
            REGISTER(Modding, MapModdingExporter)
        };
    }
    
    bool HasModdedDependencies() override { return true; }
};
} // namespace BK64
