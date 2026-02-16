#include "PropArrayFactory.h"
#include "LevelSetupFactory.h"

#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/TorchUtils.h"

namespace BK64 {

// Prop type flags
static constexpr uint8_t PROP_FLAG_ACTOR    = 0x01;  // is_actor bit
static constexpr uint8_t PROP_FLAG_3D       = 0x02;  // is_3d bit

static inline const char* GetPropTypeName(uint8_t flags) {
    if (flags & PROP_FLAG_ACTOR) return "Actor";
    if (flags & PROP_FLAG_3D) return "Model";
    return "Sprite";
}

ExportResult PropArrayHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto data = std::static_pointer_cast<PropArrayData>(raw);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern Prop " << symbol << "[" << data->mProps.size() << "];\n";
    return std::nullopt;
}

ExportResult PropArrayCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<PropArrayData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "Prop " << symbol << "[] = {\n";
    for (const auto& prop : data->mProps) {
        const uint8_t flags = prop.raw[10];
        const char* typeName = GetPropTypeName(flags);
        
        write << fourSpaceTab << "{ ." << typeName << " = { ";
        
        if (flags & PROP_FLAG_ACTOR) {
            // ActorProps should not exist in ROM data (runtime-only)
            SPDLOG_WARN("Attempting to export ActorProp - these are runtime-only!");
            write << "/* ActorProp - runtime only */";
        } else if (flags & PROP_FLAG_3D) {
            // ModelProp: unk0(2), yaw(1), roll(1), position[3](6), scale(1), flags(1)
            write << std::hex << "0x" << prop.model.unk0 << std::dec << ", ";
            write << (int)prop.model.yaw << ", " << (int)prop.model.roll << ", ";
            write << "{ " << prop.model.position[0] << ", " 
                  << prop.model.position[1] << ", " 
                  << prop.model.position[2] << " }, ";
            write << (int)prop.model.scale << ", ";
            write << std::hex << "0x" << (int)prop.model.flags << std::dec;
        } else {
            // SpriteProp: word0(4), unk4[3](6), word8(2)
            write << std::hex << "0x" << prop.sprite.word0 << std::dec << ", ";
            write << "{ " << prop.sprite.unk4[0] << ", "
                  << prop.sprite.unk4[1] << ", "
                  << prop.sprite.unk4[2] << " }, ";
            write << std::hex << "0x" << prop.sprite.word8 << std::dec;
        }
        
        write << " } },\n";
    }
    write << "};\n\n";

    return offset + (data->mProps.size() * 12);
}

ExportResult PropArrayBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto data = std::static_pointer_cast<PropArrayData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKPropArray, 0);
    
    writer.Write((uint32_t)data->mProps.size());
    
    for (const auto& prop : data->mProps) {
        const uint8_t flags = prop.raw[10];
        
        if (flags & PROP_FLAG_ACTOR) {
            // ActorProps are runtime-only, should never be in ROM
            SPDLOG_ERROR("Found ActorProp during binary export - skipping");
            continue;
        } else if (flags & PROP_FLAG_3D) {
            // ModelProp: unk0(2), yaw(1), roll(1), position[3](6), scale(1), flags(1)
            writer.Write(prop.model.unk0);
            writer.Write(prop.model.yaw);
            writer.Write(prop.model.roll);
            writer.Write(prop.model.position[0]);
            writer.Write(prop.model.position[1]);
            writer.Write(prop.model.position[2]);
            writer.Write(prop.model.scale);
            writer.Write(prop.model.flags);
        } else {
            // SpriteProp: word0(4), unk4[3](6), word8(2)
            writer.Write(prop.sprite.word0);
            writer.Write(prop.sprite.unk4[0]);
            writer.Write(prop.sprite.unk4[1]);
            writer.Write(prop.sprite.unk4[2]);
            writer.Write(prop.sprite.word8);
        }
    }
    
    writer.Finish(write);
    return OffsetEntry{ 0 };
}

std::optional<std::shared_ptr<IParsedData>> PropArrayFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto parentSymbol = GetSafeNode<std::string>(node, "parentSymbol");
    const auto cubeIndex = GetSafeNode<size_t>(node, "cubeIndex");

    // Get the parent LevelSetup data
    auto parentData = Companion::Instance->GetParseDataBySymbol(parentSymbol);
    if (!parentData.has_value() || !parentData->data.has_value()) {
        SPDLOG_ERROR("PropArrayFactory: Could not find parent LevelSetup data for symbol: {}", parentSymbol);
        return std::nullopt;
    }

    auto levelSetupData = std::static_pointer_cast<LevelSetupData>(parentData->data.value());
    if (cubeIndex >= levelSetupData->mCubes.size()) {
        SPDLOG_ERROR("PropArrayFactory: Cube index {} out of range for {}", cubeIndex, parentSymbol);
        return std::nullopt;
    }

    const auto& cube = levelSetupData->mCubes[cubeIndex];
    
    return std::make_shared<PropArrayData>(cube.props, parentSymbol, cubeIndex);
}

} // namespace BK64
