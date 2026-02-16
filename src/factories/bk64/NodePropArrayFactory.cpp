#include "NodePropArrayFactory.h"
#include "LevelSetupFactory.h"

#include "spdlog/spdlog.h"
#include "Companion.h"
#include "utils/TorchUtils.h"

namespace BK64 {

ExportResult NodePropArrayHeaderExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    const auto symbol = GetSafeNode(node, "symbol", entryName);
    auto data = std::static_pointer_cast<NodePropArrayData>(raw);

    if (Companion::Instance->IsOTRMode()) {
        write << "static const ALIGN_ASSET(2) char " << symbol << "[] = \"__OTR__" << (*replacement) << "\";\n\n";
        return std::nullopt;
    }

    write << "extern NodeProp " << symbol << "[" << data->mNodeProps.size() << "];\n";
    return std::nullopt;
}

ExportResult NodePropArrayCodeExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto offset = GetSafeNode<uint32_t>(node, "offset");
    auto data = std::static_pointer_cast<NodePropArrayData>(raw);
    const auto symbol = GetSafeNode(node, "symbol", entryName);

    write << "NodeProp " << symbol << "[] = {\n";
    for (const auto& nodeProp : data->mNodeProps) {
        write << fourSpaceTab << "{\n";
        write << fourSpaceTab << fourSpaceTab << "/* pos */ " << nodeProp.x << ", " << nodeProp.y << ", " << nodeProp.z << ",\n";
        write << fourSpaceTab << fourSpaceTab << "/* radius */ " << nodeProp.radius << ",\n";
        write << fourSpaceTab << fourSpaceTab << "/* type */ " << (uint32_t)nodeProp.bit6;
        
        if (nodeProp.bit6 == 6) {
            write << ", /* actor */ " << std::hex << "0x" << nodeProp.unk8 << std::dec;
        } else if (nodeProp.bit6 == 7) {
            write << ", /* warp */ " << std::hex << "0x" << nodeProp.unk8 << std::dec;
        } else if (nodeProp.bit6 == 9) {
            write << ", /* trigger */ " << std::hex << "0x" << nodeProp.unk8 << std::dec;
        } else if (nodeProp.bit6 == 0xA) {
            write << ", /* event */ " << std::hex << "0x" << nodeProp.unk8 << std::dec;
        } else {
            write << ", " << std::hex << "0x" << nodeProp.unk8 << std::dec;
        }
        
        write << ",\n";
        write << fourSpaceTab << fourSpaceTab << "/* yaw */ " << nodeProp.yaw << ", /* scale */ " << nodeProp.scale << "\n";
        write << fourSpaceTab << "},\n";
    }
    write << "};\n\n";

    return offset + (data->mNodeProps.size() * 20);
}

ExportResult NodePropArrayBinaryExporter::Export(std::ostream& write, std::shared_ptr<IParsedData> raw, std::string& entryName, YAML::Node& node, std::string* replacement) {
    auto writer = LUS::BinaryWriter();
    const auto data = std::static_pointer_cast<NodePropArrayData>(raw);

    WriteHeader(writer, Torch::ResourceType::BKNodePropArray, 0);
    
    writer.Write((uint32_t)data->mNodeProps.size());
    
    for (const auto& nodeProp : data->mNodeProps) {
        // Write raw position (Int16 x 3 = 6 bytes)
        writer.Write(nodeProp.x);
        writer.Write(nodeProp.y);
        writer.Write(nodeProp.z);

        // Pack flags1: radius:9, bit6:6, bit0:1 (2 bytes)
        uint16_t flags1 = ((nodeProp.radius & 0x1FF) << 7) |
                          ((nodeProp.bit6 & 0x3F) << 1)   |
                          ((nodeProp.bit0 & 0x1) << 0);
        writer.Write(flags1);

        // Identification/Padding (4 bytes)
        writer.Write(nodeProp.unk8);
        writer.Write(nodeProp.unkA);
        writer.Write(nodeProp.padB);

        // Pack flags2: yaw:9, scale:23 (4 bytes)
        uint32_t flags2 = ((nodeProp.yaw & 0x1FF) << 23) |
                          ((nodeProp.scale & 0x7FFFFF) << 0);
        writer.Write(flags2);

        // Pack flags3 (4 bytes)
        uint32_t flags3 = ((nodeProp.unk10_31 & 0xFFF) << 20) |
                          ((nodeProp.unk10_19 & 0xFFF) << 8)  |
                          ((nodeProp.pad10_7 & 0x1) << 7)     |
                          ((nodeProp.unk10_6 & 0x1) << 6)     |
                          ((nodeProp.pad10_5 & 0xF) << 2)     |
                          ((nodeProp.unk10_0 & 0x3) << 0);
        writer.Write(flags3);
    }
    
    writer.Finish(write);
    return OffsetEntry{ 0 };
}

std::optional<std::shared_ptr<IParsedData>> NodePropArrayFactory::parse(std::vector<uint8_t>& buffer, YAML::Node& node) {
    const auto symbol = GetSafeNode<std::string>(node, "symbol");
    const auto parentSymbol = GetSafeNode<std::string>(node, "parentSymbol");
    const auto cubeIndex = GetSafeNode<size_t>(node, "cubeIndex");

    // Get the parent LevelSetup data
    auto parentData = Companion::Instance->GetParseDataBySymbol(parentSymbol);
    if (!parentData.has_value() || !parentData->data.has_value()) {
        SPDLOG_ERROR("NodePropArrayFactory: Could not find parent LevelSetup data for symbol: {}", parentSymbol);
        return std::nullopt;
    }

    auto levelSetupData = std::static_pointer_cast<LevelSetupData>(parentData->data.value());
    if (cubeIndex >= levelSetupData->mCubes.size()) {
        SPDLOG_ERROR("NodePropArrayFactory: Cube index {} out of range for {}", cubeIndex, parentSymbol);
        return std::nullopt;
    }

    const auto& cube = levelSetupData->mCubes[cubeIndex];
    
    return std::make_shared<NodePropArrayData>(cube.nodeProps, parentSymbol, cubeIndex);
}

} // namespace BK64
