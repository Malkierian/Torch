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
 * - Processed during level load (cubeList_fromFile in actor_cubebounds.c) to create ActorMarkers
 * - category field determines behavior:
 *   * 6 = Actor spawn point (creates dynamic entity via markerActorTypeArray dispatch)
 *   * 7 = Warp destination (teleports player to different map)
 *   * 9 = Trigger zone (activates events when player enters radius)
 *   * 0xA = Special event marker (used by level-specific systems)
 * 
 * Data Flow:
 *   ROM → MapFactory parse() → NodeProp array → cubeList_fromFile →
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
    int16_t position[3];    // X, Y, Z world position (s16 coordinates)
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
 * Complete Structure Layout (12 bytes):
 *   Offset 0x00-0x03: word0 (32-bit packed) - sprite appearance/rendering parameters
 *   Offset 0x04-0x09: unk4[3] (s16[3]) - X, Y, Z world position
 *   Offset 0x0A-0x0B: wordA (16-bit packed) - animation frame + discriminator flags
 * 
 * Packed Field Layout (word0 - 32-bit big-endian at offset 0x00):
 *   Bits 31-20: spriteId (12 bits) → Asset ID = spriteId + SPRITE_ASSET_OFFSET (0x572)
 *   Bit 19: unk0_19 (1 bit)
 *   Bits 18-16: rgb_remove_red (3 bits, 0-7 color removal value)
 *   Bits 15-13: rgb_remove_green (3 bits, 0-7 color removal value)
 *   Bits 12-10: rgb_remove_blue (3 bits, 0-7 color removal value)
 *   Bits 9-2: scale (8 bits)
 *   Bit 1: isMirrored (1 bit, horizontal flip)
 *   Bit 0: pad0_0 (1 bit)
 * 
 * Packed Field Layout (wordA - 16-bit big-endian at offset 0x0A):
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
    uint32_t word0;
    int16_t unk4[3];
    uint16_t wordA;
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
 * NOTE: ActorProps DO appear in ROM. The `marker` field (bytes 0-3) is always NULL
 *       in ROM and gets populated at runtime when the engine spawns the actor.
 *       Position and flags fields are valid ROM data.
 */
typedef struct ActorProp {
    uint32_t marker;        // ActorMarker* pointer (runtime only)
    int16_t position[3];    // X, Y, Z position cache
    uint16_t flags;         // Discriminator flags (isActorProp=1)
} ActorProp;

/**
 * Prop: Union of ModelProp, SpriteProp, ActorProp (12 bytes)
 * 
 * Type discrimination (flags at offset 0x0A, bits 0-1):
 *   isActorProp=1 → Treated as ActorProp (has actor marker metadata)
 *   isActorProp=0, isModelProp=1 → ModelProp (static 3D geometry)
 *   isActorProp=0, isModelProp=0 → SpriteProp (2D billboard sprite)
 * 
 * All three types are 12 bytes and are stored in ROM. The flags determine interpretation.
 */
typedef union Prop {
    ModelProp model;
    SpriteProp sprite;
    ActorProp actor;
    struct {
        uint32_t pad0;
        int16_t unk4[3];
        uint16_t padA_15: 10;
        uint16_t unkA_5: 1;
        uint16_t unkA_4: 1;
        uint16_t unkA_3: 1;
        uint16_t unkA_2: 1;
        uint16_t is_3d: 1;
        uint16_t is_actor: 1;
    };
    uint8_t raw[12];
} Prop;

/**
 * CubeData: Spatial partition cell in 32×32×32 grid
 * 
 * Grid coordinates: 5-bit values (0-31) for x/y/z
 * unk0_4: Boundary separating regular NodeProps [0..unk0_4) from events [unk0_4..prop1Cnt)
 */
typedef struct CubeData {
    int32_t x: 5;
    int32_t y: 5;
    int32_t z: 5;
    uint32_t prop1Cnt: 6;
    uint32_t prop2Cnt: 6;
    uint32_t unk0_4: 5;     // Boundary: [0..unk0_4)=spawns, [unk0_4..prop1Cnt)=events
    std::vector<NodeProp> nodeProps;
    std::vector<Prop> props;
} CubeData;

/**
 * CameraNodeType1: Scripted/path camera (CAMERA_TYPE_1_UNKNOWN)
 * Decomp: CameraNodeType1 from camera.h
 * Size: 44 bytes (11 floats + 1 s32)
 * 
 * Usage: Likely used for cutscenes and scripted camera sequences.
 * Has no per-frame update handler - camera follows predefined path using
 * position, speed, acceleration, and orientation parameters.
 */
typedef struct CameraNodeType1 {
    float position[3];      // Camera position
    float horizontalSpeed;  // Horizontal movement speed
    float verticalSpeed;    // Vertical movement speed
    float rotation;         // Rotation speed
    float accelaration;     // Acceleration value
    float pitchYawRoll[3];  // Camera orientation (pitch, yaw, roll)
    int32_t unknownFlag;    // Unknown flag (anded with 1, 2, 4)
} CameraNodeType1;

/**
 * CameraNodeType2: Dynamic camera behavior (CAMERA_TYPE_2_DYNAMIC)
 * Decomp: CameraNodeType2 from camera.h
 * Size: 24 bytes (6 floats)
 */
typedef struct CameraNodeType2 {
    float position[3];      // Camera position
    float pitchYawRoll[3];  // Camera orientation (pitch, yaw, roll)
} CameraNodeType2;

/**
 * CameraNodeType3: Static camera behavior (CAMERA_TYPE_3_STATIC)
 * Decomp: CameraNodeType3 from camera.h
 * Size: 52 bytes (12 floats + 1 s32)
 */
typedef struct CameraNodeType3 {
    float position[3];      // Camera position
    float horizontalSpeed;  // Horizontal movement speed
    float verticalSpeed;    // Vertical movement speed
    float rotation;         // Rotation speed
    float accelaration;     // Acceleration value
    float closeDistance;    // Near clipping distance
    float farDistance;      // Far clipping distance
    float pitchYawRoll[3];  // Camera orientation (pitch, yaw, roll)
    int32_t unknownFlag;    // Unknown flag
} CameraNodeType3;

/**
 * CameraNodeType4: Random camera behavior (CAMERA_TYPE_4_RANDOM)
 * Decomp: CameraNodeType4 from camera.h
 * Size: 4 bytes (1 s32)
 */
typedef struct CameraNodeType4 {
    int32_t unknownFlag;    // Unknown flag value
} CameraNodeType4;

/**
 * CameraNode: Camera path/behavior node
 * Decomp: CameraNode from camera.h
 * Format: marker 0x01 + index (s16) + type marker 0x02 + type (u8) + type-specific data
 * 
 * Camera types:
 *   1 = Scripted/path camera (cutscenes, camera paths)
 *   2 = Dynamic camera (follows player)
 *   3 = Static camera (fixed position)
 *   4 = Random camera (special behavior)
 */
typedef struct CameraNode {
    int16_t index;          // Camera node index
    uint8_t type;           // Node type (1=Scripted, 2=Dynamic, 3=Static, 4=Random)
    union {
        CameraNodeType1 type1;  // Type 1 = Scripted/path camera for cutscenes
        CameraNodeType2 type2;  // Type 2 = Dynamic camera
        CameraNodeType3 type3;  // Type 3 = Static camera
        CameraNodeType4 type4;  // Type 4 = Random camera
    } data;
} CameraNode;

/**
 * LightingVector: Point light with fade radius
 * Decomp: Lighting from gclights.c
 * Format: marker 0x01 + position (f32[3]) + fade_radii (f32[2]) + rgb (s32[3])
 */
typedef struct LightingVector {
    float position[3];      // X, Y, Z light position
    float fadeRadii[2];     // Inner/outer fade distances
    int32_t rgb[3];         // R, G, B color values
} LightingVector;

class MapData : public IParsedData {
  public:
    // Cube data section (chunk type 0x01)
    int32_t mCubeMin[3];    // Grid minimum bounds
    int32_t mCubeMax[3];    // Grid maximum bounds
    std::vector<CubeData> mCubes;
    
    // Camera nodes section (chunk type 0x03)
    std::vector<CameraNode> mCameraNodes;
    
    // Lighting section (chunk type 0x04)
    std::vector<LightingVector> mLightingVectors;

    MapData() {
        mCubeMin[0] = mCubeMin[1] = mCubeMin[2] = 0;
        mCubeMax[0] = mCubeMax[1] = mCubeMax[2] = 0;
    }
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
