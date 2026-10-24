#include "object_properties.h"
#include "iltserver.h"
#include "world_state.h"
#include "engine/portable/binary.h"
#include "engine/portable/command_reader.h"
#include <cmath>
#include <stdexcept>

void gp_SetString(GenericProp*, char*);
void gp_SetVector(GenericProp*, LTVector*);
void gp_SetFloat(GenericProp*, float);
void gp_SetRotation(GenericProp*, LTVector*);
namespace {
void validate_name(std::string_view name) {
    if (name.empty() || name.size() > 255 || name.find('\0') != name.npos)
        throw std::runtime_error("invalid object property name");
}
}
ObjectProperties::ObjectProperties(std::span<const WorldProperty> source) {
    values_.reserve(source.size());
    for (const auto& encoded : source) {
        ObjectProperty property;
        validate_name(encoded.name);
        property.name = encoded.name; property.type = encoded.type;
        nolf::BinaryReader reader(encoded.data);
        switch (property.type) {
        case PT_STRING: {
            const auto bytes = reader.bytes(reader.integer<uint16>());
            property.text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            if (property.text.find('\0') != property.text.npos) throw std::runtime_error("embedded NUL in object property");
            gp_SetString(&property.value, property.text.data());
            break;
        }
        case PT_VECTOR: case PT_COLOR: case PT_ROTATION: {
            LTVector vector;
            for (auto axis = 0; axis < 3; ++axis) {
                vector[axis] = reader.real();
                if (!std::isfinite(vector[axis])) throw std::runtime_error("nonfinite object vector property");
            }
            if (property.type == PT_ROTATION) {
                reader.real(); // Original rotation record has a fourth unused float.
                gp_SetRotation(&property.value, &vector);
            } else gp_SetVector(&property.value, &vector);
            break;
        }
        case PT_BOOL:
            gp_SetFloat(&property.value, reader.integer<uint8>() != 0 ? 1.f : 0.f);
            break;
        case PT_REAL: case PT_FLAGS: case PT_LONGINT: {
            const auto value = reader.real();
            if (!std::isfinite(value)) throw std::runtime_error("nonfinite numeric object property");
            gp_SetFloat(&property.value, value);
            break;
        }
        default: throw std::runtime_error("unknown object property type");
        }
        if (reader.remaining()) throw std::runtime_error("unexpected object property payload size");
        values_.push_back(std::move(property));
    }
}
ObjectProperties::ObjectProperties(std::string_view statements) {
    if (statements.size() > 1024 * 1024 || statements.find('\0') != statements.npos)
        throw std::runtime_error("invalid object property statements");
    nolf::CommandReader reader(statements);
    while (const auto args = reader.next()) {
        if (reader.overflowed()) throw std::runtime_error("object property statement exceeds SDK bounds");
        if (args->empty()) continue;
        ObjectProperty property;
        property.name = args->front(); property.type = PT_STRING;
        validate_name(property.name);
        for (std::size_t index = 1; index < args->size(); ++index) {
            if (index != 1) property.text += ' ';
            property.text += (*args)[index];
        }
        gp_SetString(&property.value, property.text.data());
        values_.push_back(std::move(property));
    }
    if (reader.overflowed()) throw std::runtime_error("object property statement exceeds SDK bounds");
}
const ObjectProperty* ObjectProperties::find(std::string_view name) const {
    // The old linked list inserted at its head: the last definition wins.
    for (auto value = values_.rbegin(); value != values_.rend(); ++value)
        if (value->name == name) return &*value;
    return nullptr;
}
