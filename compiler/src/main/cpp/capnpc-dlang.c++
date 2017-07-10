// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// This program is a code generator plugin for `capnp compile` which generates D code.
// It is a modified version of the C++ code generator plugin, capnpc-c++.


#include <capnp/schema.capnp.h>
#include <capnp/serialize.h>
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/string-tree.h>
#include <kj/vector.h>
#include <capnp/schema-loader.h>
#include <capnp/dynamic.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <kj/main.h>
#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#if HAVE_CONFIG_H
#include "config.h"
#endif

#if CAPNP_VERSION < 5000
#error "This version of capnpc-dlang requires Cap'n Proto version 0.5 or higher."
#endif

#ifndef VERSION
#define VERSION "(unknown)"
#endif

#if _WIN32
#define mkdir(path, mode) mkdir(path)
#endif

namespace capnp {
namespace {

static constexpr uint64_t MODULE_ANNOTATION_ID = 0x9ee5c8f803b3b596ull;

static constexpr const char* FIELD_SIZE_NAMES[] = {
  "VOID", "BIT", "BYTE", "TWO_BYTES", "FOUR_BYTES", "EIGHT_BYTES", "POINTER", "INLINE_COMPOSITE"
};

bool hasDiscriminantValue(const schema::Field::Reader& reader) {
  return reader.getDiscriminantValue() != schema::Field::NO_DISCRIMINANT;
}

void enumerateDeps(schema::Type::Reader type, std::set<uint64_t>& deps) {
  switch (type.which()) {
    case schema::Type::STRUCT:
      deps.insert(type.getStruct().getTypeId());
      break;
    case schema::Type::ENUM:
      deps.insert(type.getEnum().getTypeId());
      break;
    case schema::Type::INTERFACE:
      deps.insert(type.getInterface().getTypeId());
      break;
    case schema::Type::LIST:
      enumerateDeps(type.getList().getElementType(), deps);
      break;
    default:
      break;
  }
}

void enumerateDeps(schema::Node::Reader node, std::set<uint64_t>& deps) {
  switch (node.which()) {
    case schema::Node::STRUCT: {
      auto structNode = node.getStruct();
      for (auto field: structNode.getFields()) {
        switch (field.which()) {
          case schema::Field::SLOT:
            enumerateDeps(field.getSlot().getType(), deps);
            break;
          case schema::Field::GROUP:
            deps.insert(field.getGroup().getTypeId());
            break;
        }
      }
      if (structNode.getIsGroup()) {
        deps.insert(node.getScopeId());
      }
      break;
    }
    case schema::Node::INTERFACE: {
      auto interfaceNode = node.getInterface();
      for (auto superclass: interfaceNode.getSuperclasses()) {
        deps.insert(superclass.getId());
      }
      for (auto method: interfaceNode.getMethods()) {
        deps.insert(method.getParamStructType());
        deps.insert(method.getResultStructType());
      }
      break;
    }
    default:
      break;
  }
}

struct OrderByName {
  template <typename T>
  inline bool operator()(const T& a, const T& b) const {
    return a.getProto().getName() < b.getProto().getName();
  }
};

template <typename MemberList>
kj::Array<uint> makeMembersByName(MemberList&& members) {
  auto sorted = KJ_MAP(member, members) { return member; };
  std::sort(sorted.begin(), sorted.end(), OrderByName());
  return KJ_MAP(member, sorted) { return member.getIndex(); };
}

kj::StringPtr baseName(kj::StringPtr path) {
  KJ_IF_MAYBE(slashPos, path.findLast('/')) {
    return path.slice(*slashPos + 1);
  } else {
    return path;
  }
}

kj::String safeIdentifier(kj::StringPtr identifier) {
  // Given a desired identifier name, munge it to make it safe for use in generated code.
  //
  // If the identifier is a keyword, this adds an underscore to the end.

  static const std::set<kj::StringPtr> keywords({
    "abstract", "alias", "align", "asm", "assert", "auto", "body", "bool", "break", "byte",
    "case", "cast", "catch", "cdouble", "cent", "cfloat", "char", "class", "const", "continue", "creal",
    "dchar", "debug", "default", "delegate", "delete", "deprecated", "do", "double",
    "else", "enum", "export", "extern", "false", "final", "finally", "float", "for", "foreach", "foreach_reverse", "function",
    "goto", "idouble", "if", "ifloat", "immutable", "import", "in", "inout", "int", "interface", "invariant", "ireal", "is",
    "lazy", "long", "macro", "mixin", "module", "new", "nothrow", "null",
    "out", "override", "package", "pragma", "private", "protected", "public", "pure",
    "real", "ref", "return", "scope", "shared", "short", "static",
    "struct", "super", "switch", "synchronized", "template", "this", "throw", "true",
    "try", "typedef", "typeid", "typeof", "ubyte", "ucent", "uint", "ulong", "union", "unittest", "ushort",
    "version", "void", "volatile", "wchar", "while", "with"
  });

  if (keywords.count(identifier) > 0) {
    return kj::str(identifier, '_');
  } else {
    return kj::heapString(identifier);
  }
}

  kj::String spaces(int n) {
    return kj::str(kj::repeat('\t', n));
  }

// =======================================================================================

class CapnpcDlangMain {
public:
  CapnpcDlangMain(kj::ProcessContext& context): context(context) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "Cap'n Proto D plugin version " VERSION,
          "This is a Cap'n Proto compiler plugin which generates D code."
          " This is meant to be run using the Cap'n Proto compiler, e.g.:\n"
          "    capnp compile -odlang foo.capnp")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

private:
  kj::ProcessContext& context;
  SchemaLoader schemaLoader;
  std::unordered_set<uint64_t> usedImports;
  bool hasInterfaces = false;

  kj::StringTree dlangFullName(Schema schema, kj::String suffix) {
    auto node = schema.getProto();
    if (node.getScopeId() == 0) {
      usedImports.insert(node.getId());
      /*kj::String className;
      for (auto annotation: node.getAnnotations()) {
        if (annotation.getId() == MODULE_ANNOTATION_ID) {
          className = annotation.getValue().getText();
        }
      }*/
      return kj::strTree();
    } else {
      Schema parent = schemaLoader.get(node.getScopeId());
      for (auto nested: parent.getProto().getNestedNodes()) {
        if (nested.getId() == node.getId()) {
          if(parent.getProto().getIsGeneric())
		  {
          auto typeArgs = getTypeArguments(schema, parent, kj::str(suffix));
          return kj::strTree(dlangFullName(parent, kj::str(suffix)), "!(", kj::StringTree(KJ_MAP(arg, typeArgs) { return kj::strTree(arg); }, ", "), ").", nested.getName());
		  }
		  else
			  return kj::strTree(dlangFullName(parent, kj::str(suffix)), ".", nested.getName());
        }
      }
      KJ_FAIL_REQUIRE("A schema Node's supposed scope did not contain the node as a NestedNode.");
    }
  }

  kj::Vector<kj::String> getTypeParameters(Schema schema) {
    auto node = schema.getProto();
    kj::Vector<kj::String> result;
    /*if (node.getScopeId() != 0) {
      Schema parent = schemaLoader.get(node.getScopeId());
      result = getTypeParameters(parent);
    }*/
    for (auto parameter : node.getParameters()) {
      result.add(kj::str(parameter.getName(), "_", kj::hex(node.getId())));
    }
    return kj::mv(result);
  }

  kj::Vector<kj::String> getTypeArguments(Schema leaf, Schema schema, kj::String suffix) {
    auto node = schema.getProto();
    kj::Vector<kj::String> result;
    /*if (node.getScopeId() != 0) {
      Schema parent = schemaLoader.get(node.getScopeId());
      result = getTypeArguments(leaf, parent, kj::str(suffix));
    }*/
    auto brandArguments = leaf.getBrandArgumentsAtScope(node.getId());
    auto parameters = node.getParameters();
    for (int ii = 0; ii < parameters.size(); ++ii) {
      result.add(typeName(brandArguments[ii], kj::str(suffix)).flatten());
    }
    return kj::mv(result);
  }

  kj::Vector<kj::String> getFactoryArguments(Schema leaf, Schema schema) {
    auto node = schema.getProto();
    kj::Vector<kj::String> result;
    if (node.getScopeId() != 0) {
      Schema parent = schemaLoader.get(node.getScopeId());
      result = getFactoryArguments(leaf, parent);
    }
    auto brandArguments = leaf.getBrandArgumentsAtScope(node.getId());
    auto parameters = node.getParameters();
    for (int ii = 0; ii < parameters.size(); ++ii) {
      result.add(makeFactoryArg(brandArguments[ii]));
    }
    return kj::mv(result);
  }

  kj::String toTitleCase(kj::StringPtr name) {
    kj::String result = kj::heapString(name);
    if ('a' <= result[0] && result[0] <= 'z') {
      result[0] = result[0] - 'a' + 'A';
    }
    return kj::mv(result);
  }

  kj::StringTree typeName(capnp::Type type, kj::String suffix = nullptr) {
    switch (type.which()) {
    case schema::Type::VOID: return kj::strTree("Void");

    case schema::Type::BOOL: return kj::strTree("bool");
    case schema::Type::INT8: return kj::strTree("byte");
    case schema::Type::INT16: return kj::strTree("short");
    case schema::Type::INT32: return kj::strTree("int");
    case schema::Type::INT64: return kj::strTree("long");
    case schema::Type::UINT8: return kj::strTree("ubyte");
    case schema::Type::UINT16: return kj::strTree("ushort");
    case schema::Type::UINT32: return kj::strTree("uint");
    case schema::Type::UINT64: return kj::strTree("ulong");
    case schema::Type::FLOAT32: return kj::strTree("float");
    case schema::Type::FLOAT64: return kj::strTree("double");

    case schema::Type::TEXT: return kj::strTree("Text", suffix != nullptr? "." : "", suffix);
    case schema::Type::DATA: return kj::strTree("Data", suffix != nullptr? "." : "", suffix);

    case schema::Type::ENUM: return dlangFullName(type.asEnum(), kj::str(suffix));
    case schema::Type::STRUCT: {
      auto structSchema = type.asStruct();
      if (structSchema.getProto().getIsGeneric()) {
        auto typeArgs = getTypeArguments(structSchema, structSchema, /*kj::str("Type")*/kj::str(suffix));
        return kj::strTree(
          dlangFullName(structSchema, kj::str(suffix)), (typeArgs.size() > 0? kj::strTree("!(",
          kj::StringTree(KJ_MAP(arg, typeArgs){
              return kj::strTree(arg);
            }, ", "),
          ")") : kj::strTree("")), suffix != nullptr? "." : "", suffix
          );
      } else {
        return kj::strTree(dlangFullName(type.asStruct(), kj::str(suffix)), suffix != nullptr? "." : "", suffix);
      }
    }
    case schema::Type::INTERFACE:
      return dlangFullName(type.asInterface(), kj::str(suffix));

    case schema::Type::LIST:
    {
      auto elementType = type.asList().getElementType();
      switch (elementType.which()) {
      case schema::Type::VOID:
        return kj::strTree("PrimitiveList!Void", suffix != nullptr? "." : "", suffix);
      case schema::Type::BOOL:
        return kj::strTree("PrimitiveList!bool", suffix != nullptr? "." : "", suffix);
      case schema::Type::INT8:
        return kj::strTree("PrimitiveList!byte", suffix != nullptr? "." : "", suffix);
      case schema::Type::UINT8:
        return kj::strTree("PrimitiveList!ubyte", suffix != nullptr? "." : "", suffix);
      case schema::Type::INT16:
        return kj::strTree("PrimitiveList!short", suffix != nullptr? "." : "", suffix);
      case schema::Type::UINT16:
        return kj::strTree("PrimitiveList!ushort", suffix != nullptr? "." : "", suffix);
      case schema::Type::INT32:
        return kj::strTree("PrimitiveList!int", suffix != nullptr? "." : "", suffix);
      case schema::Type::UINT32:
        return kj::strTree("PrimitiveList!uint", suffix != nullptr? "." : "", suffix);
      case schema::Type::INT64:
        return kj::strTree("PrimitiveList!long", suffix != nullptr? "." : "", suffix);
      case schema::Type::UINT64:
        return kj::strTree("PrimitiveList!ulong", suffix != nullptr? "." : "", suffix);
      case schema::Type::FLOAT32:
        return kj::strTree("PrimitiveList!float", suffix != nullptr? "." : "", suffix);
      case schema::Type::FLOAT64:
        return kj::strTree("PrimitiveList!double", suffix != nullptr? "." : "", suffix);
      case schema::Type::STRUCT:
      {
        auto inner = typeName(elementType/*, kj::str(suffix)*/);
        return kj::strTree("StructList", "!(", kj::mv(inner), ")", suffix != nullptr? "." : "", suffix);
      }
      case schema::Type::TEXT:
        return kj::strTree( "TextList", suffix != nullptr? "." : "", suffix);
      case schema::Type::DATA:
        return kj::strTree( "DataList", suffix != nullptr? "." : "", suffix);
      case schema::Type::ENUM:
      {
        auto inner = typeName(elementType, kj::str(suffix));
        return kj::strTree("EnumList", "!(", kj::mv(inner), ")", suffix != nullptr? "." : "", suffix);
      }
      case schema::Type::LIST:
      {
        auto inner = typeName(elementType/*, kj::str(suffix)*/);
        return kj::strTree("ListList", "!(", kj::mv(inner), ")", suffix != nullptr? "." : "", suffix);
      }
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        KJ_FAIL_REQUIRE("unimplemented");
      }
      KJ_UNREACHABLE;
    }
    case schema::Type::ANY_POINTER: {
      KJ_IF_MAYBE(brandParam, type.getBrandParameter()) {
        return
          kj::strTree(schemaLoader.get(brandParam->scopeId).getProto().getParameters()[brandParam->index].getName(),
                      "_", kj::hex(brandParam->scopeId), "_", kj::str("Type") /*suffix*/);

      } else {
        return kj::strTree("AnyPointer", suffix != nullptr? "." : "", suffix);
      }
    }
    }
    KJ_UNREACHABLE;
  }

  kj::StringTree literalValue(schema::Type::Reader type, schema::Value::Reader value) {
    switch (value.which()) {
      case schema::Value::VOID: return kj::strTree("Void.VOID");
      case schema::Value::BOOL: return kj::strTree(value.getBool() ? "true" : "false");
      case schema::Value::INT8: return kj::strTree(value.getInt8());
      case schema::Value::INT16: return kj::strTree(value.getInt16());
      case schema::Value::INT32: return kj::strTree(value.getInt32());
      case schema::Value::INT64: return kj::strTree(value.getInt64(), "L");
      case schema::Value::UINT8: return kj::strTree(value.getUint8());
      case schema::Value::UINT16: return kj::strTree(value.getUint16());
      case schema::Value::UINT32: return kj::strTree(value.getUint32());
      case schema::Value::UINT64: return kj::strTree(value.getUint64(), "UL");
      case schema::Value::FLOAT32: return kj::strTree(value.getFloat32(), "f");
      case schema::Value::FLOAT64: return kj::strTree(value.getFloat64());
      case schema::Value::ENUM: {
        EnumSchema schema = schemaLoader.get(type.getEnum().getTypeId()).asEnum();
        if (value.getEnum() < schema.getEnumerants().size()) {
          return kj::strTree(
              dlangFullName(schema, kj::str("")), ".",
              schema.getEnumerants()[value.getEnum()].getProto().getName());
        } else {
          return kj::strTree("static_cast<", dlangFullName(schema, kj::str("")), ">(", value.getEnum(), ")");
        }
      }

      case schema::Value::TEXT:
      case schema::Value::DATA:
      case schema::Value::STRUCT:
      case schema::Value::INTERFACE:
      case schema::Value::LIST:
      case schema::Value::ANY_POINTER:
        KJ_FAIL_REQUIRE("literalValue() can only be used on primitive types.");
    }
    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------
  // Code to deal with "slots" -- determines what to zero out when we clear a group.

  static uint typeSizeBits(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::BOOL: return 1;
      case schema::Type::INT8: return 8;
      case schema::Type::INT16: return 16;
      case schema::Type::INT32: return 32;
      case schema::Type::INT64: return 64;
      case schema::Type::UINT8: return 8;
      case schema::Type::UINT16: return 16;
      case schema::Type::UINT32: return 32;
      case schema::Type::UINT64: return 64;
      case schema::Type::FLOAT32: return 32;
      case schema::Type::FLOAT64: return 64;
      case schema::Type::ENUM: return 16;

      case schema::Type::VOID:
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        KJ_FAIL_REQUIRE("Should only be called for data types.");
    }
    KJ_UNREACHABLE;
  }

  enum class Section {
    NONE,
    DATA,
    POINTERS
  };

  static Section sectionFor(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::VOID:
        return Section::NONE;
      case schema::Type::BOOL:
      case schema::Type::INT8:
      case schema::Type::INT16:
      case schema::Type::INT32:
      case schema::Type::INT64:
      case schema::Type::UINT8:
      case schema::Type::UINT16:
      case schema::Type::UINT32:
      case schema::Type::UINT64:
      case schema::Type::FLOAT32:
      case schema::Type::FLOAT64:
      case schema::Type::ENUM:
        return Section::DATA;
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        return Section::POINTERS;
    }
    KJ_UNREACHABLE;
  }

  static kj::StringPtr maskType(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::BOOL: return "bool";
      case schema::Type::INT8: return "byte";
      case schema::Type::INT16: return "short";
      case schema::Type::INT32: return "int";
      case schema::Type::INT64: return "long";
      case schema::Type::UINT8: return "ubyte";
      case schema::Type::UINT16: return "ushort";
      case schema::Type::UINT32: return "uint";
      case schema::Type::UINT64: return "ulong";
      case schema::Type::FLOAT32: return "int";
      case schema::Type::FLOAT64: return "long";
      case schema::Type::ENUM: return "ushort";

      case schema::Type::VOID:
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        KJ_FAIL_REQUIRE("Should only be called for data types.");
    }
    KJ_UNREACHABLE;
  }

  static kj::StringPtr maskZeroLiteral(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::BOOL: return "false";
      case schema::Type::INT8: return "cast(byte)0";
      case schema::Type::INT16: return "cast(short)0";
      case schema::Type::INT32: return "0";
      case schema::Type::INT64: return "0L";
      case schema::Type::UINT8: return "cast(ubyte)0";
      case schema::Type::UINT16: return "cast(ushort)0";
      case schema::Type::UINT32: return "0U";
      case schema::Type::UINT64: return "0UL";
      case schema::Type::FLOAT32: return "0f";
      case schema::Type::FLOAT64: return "0.0";
      case schema::Type::ENUM: return "cast(ushort)0";

      case schema::Type::VOID:
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        KJ_FAIL_REQUIRE("Should only be called for data types.");
    }
    KJ_UNREACHABLE;
  }


  struct Slot {
    schema::Type::Which whichType;
    uint offset;

    bool isSupersetOf(Slot other) const {
      auto section = sectionFor(whichType);
      if (section != sectionFor(other.whichType)) return false;
      switch (section) {
        case Section::NONE:
          return true;  // all voids overlap
        case Section::DATA: {
          auto bits = typeSizeBits(whichType);
          auto start = offset * bits;
          auto otherBits = typeSizeBits(other.whichType);
          auto otherStart = other.offset * otherBits;
          return start <= otherStart && otherStart + otherBits <= start + bits;
        }
        case Section::POINTERS:
          return offset == other.offset;
      }
      KJ_UNREACHABLE;
    }

    bool operator<(Slot other) const {
      // Sort by section, then start position, and finally size.

      auto section = sectionFor(whichType);
      auto otherSection = sectionFor(other.whichType);
      if (section < otherSection) {
        return true;
      } else if (section > otherSection) {
        return false;
      }

      switch (section) {
        case Section::NONE:
          return false;
        case Section::DATA: {
          auto bits = typeSizeBits(whichType);
          auto start = offset * bits;
          auto otherBits = typeSizeBits(other.whichType);
          auto otherStart = other.offset * otherBits;
          if (start < otherStart) {
            return true;
          } else if (start > otherStart) {
            return false;
          }

          // Sort larger sizes before smaller.
          return bits > otherBits;
        }
        case Section::POINTERS:
          return offset < other.offset;
      }
      KJ_UNREACHABLE;
    }
  };

  void getSlots(StructSchema schema, kj::Vector<Slot>& slots) {
    auto structProto = schema.getProto().getStruct();
    if (structProto.getDiscriminantCount() > 0) {
      slots.add(Slot { schema::Type::UINT16, structProto.getDiscriminantOffset() });
    }

    for (auto field: schema.getFields()) {
      auto proto = field.getProto();
      switch (proto.which()) {
        case schema::Field::SLOT: {
          auto slot = proto.getSlot();
          slots.add(Slot { slot.getType().which(), slot.getOffset() });
          break;
        }
        case schema::Field::GROUP:
          getSlots(field.getType().asStruct(), slots);
          break;
      }
    }
  }

  kj::Array<Slot> getSortedSlots(StructSchema schema) {
    // Get a representation of all of the field locations owned by this schema, e.g. so that they
    // can be zero'd out.

    kj::Vector<Slot> slots(schema.getFields().size());
    getSlots(schema, slots);
    std::sort(slots.begin(), slots.end());

    kj::Vector<Slot> result(slots.size());

    // All void slots are redundant, and they sort towards the front of the list.  By starting out
    // with `prevSlot` = void, we will end up skipping them all, which is what we want.
    Slot prevSlot = { schema::Type::VOID, 0 };
    for (auto slot: slots) {
      if (prevSlot.isSupersetOf(slot)) {
        // This slot is redundant as prevSlot is a superset of it.
        continue;
      }

      // Since all sizes are power-of-two, if two slots overlap at all, one must be a superset of
      // the other.  Since we sort slots by starting position, we know that the only way `slot`
      // could be a superset of `prevSlot` is if they have the same starting position.  However,
      // since we sort slots with the same starting position by descending size, this is not
      // possible.
      KJ_DASSERT(!slot.isSupersetOf(prevSlot));

      result.add(slot);

      prevSlot = slot;
    }

    return result.releaseAsArray();
  }

  // -----------------------------------------------------------------

  struct DiscriminantChecks {
    kj::String has;
    kj::String check;
    kj::String set;
    kj::StringTree readerIsDef;
    kj::StringTree builderIsDef;
  };

  DiscriminantChecks makeDiscriminantChecks(kj::StringPtr scope,
                                            kj::StringPtr memberName,
                                            StructSchema containingStruct,
                                            int indent) {
    auto discrimOffset = containingStruct.getProto().getStruct().getDiscriminantOffset();

    kj::String titleCase = toTitleCase(memberName);
    kj::String upperCase = safeIdentifier(memberName);

    return DiscriminantChecks {
      kj::str(spaces(indent+1),
              "if(which() != ", scope, "Which.", upperCase, ") return false;\n"),
        kj::str(
          spaces(indent+1),
          "assert(which() == ", scope, "Which.", upperCase, ", \"Must check which() before get()ing a union member.\");\n"),
        kj::str(
          spaces(indent+1), "b._setShortField(", discrimOffset, ", cast(ushort)",
          scope, "Which.", upperCase, ");\n"),
          kj::strTree(spaces(indent), "bool is", titleCase, "()\n", spaces(indent), "{\n",
                      spaces(indent+1), "return which() == ", scope, "Which.", upperCase,";\n",
                      spaces(indent), "}\n"),
          kj::strTree(spaces(indent), "bool is", titleCase, "()\n", spaces(indent), "{\n",
                      spaces(indent+1), "return which() == ", scope, "Which.", upperCase, ";\n",
                      spaces(indent), "}\n")
    };
  }

  // -----------------------------------------------------------------

  struct FieldText {
    kj::StringTree readerMethodDecls;
    kj::StringTree builderMethodDecls;
  };

  enum class FieldKind {
    PRIMITIVE,
    BLOB,
    STRUCT,
    LIST,
    INTERFACE,
    ANY_POINTER
  };

  kj::StringTree makeEnumGetter(EnumSchema schema, uint offset, kj::String defaultMaskParam, int indent) {
    auto enumerants = schema.getEnumerants();
    return kj::strTree(
      spaces(indent), "switch(b._getUshortField(", offset, defaultMaskParam, "))\n", spaces(indent), "{\n",
      KJ_MAP(e, enumerants) {
        return kj::strTree(spaces(indent+1), "case ", e.getOrdinal(), ": return ",
                           dlangFullName(schema, kj::str("")), ".", safeIdentifier(e.getProto().getName()), ";\n");
      },
      spaces(indent+1), "default: return ", dlangFullName(schema, kj::str("")), "._NOT_IN_SCHEMA;\n",
      spaces(indent), "}\n"
      );
  }

  kj::String makeFactoryArg(capnp::Type type) {
    switch (type.which()) {
    case schema::Type::TEXT : {
      return kj::str("cast(Text.Factory)Text.factory");
    }
    case schema::Type::DATA : {
      return kj::str("cast(Data.Factory)Data.factory");
    }
    case schema::Type::ANY_POINTER : {
      KJ_IF_MAYBE(brandParam, type.getBrandParameter()) {
        return
          kj::str(schemaLoader.get(brandParam->scopeId).getProto().getParameters()[brandParam->index].getName(),
                  "_", kj::hex(brandParam->scopeId), "_Factory");

      } else {
        return kj::str("cast(AnyPointer.Factory)AnyPointer.factory");
      }
    }
    case schema::Type::STRUCT : {
      auto structSchema = type.asStruct();
      auto node = structSchema.getProto();
      if (node.getIsGeneric()) {
        auto factoryArgs = getFactoryArguments(structSchema, structSchema);
        return kj::strTree(
          dlangFullName(structSchema, kj::str("")), ".newFactory(",
          kj::StringTree(
            KJ_MAP(arg, factoryArgs) {
              return kj::strTree(arg);
            }, ","),
          ")"
          ).flatten();
      } else {
        return kj::str(kj::strTree("cast(", typeName(type, kj::str("Factory")), ")", typeName(type, kj::str("factory"))));
      }
    }
    case schema::Type::LIST: {
      auto elementType = type.asList().getElementType();
      switch (elementType.which()) {
      case schema::Type::STRUCT: {
        auto elementStructSchema = elementType.asStruct();
        auto elementNode = elementStructSchema.getProto();
        if (elementNode.getIsGeneric()) {
          auto factoryArgs = getFactoryArguments(elementStructSchema, elementStructSchema);
          return kj::strTree(
            "new StructList.Factory!(",
            typeName(elementType, kj::str("Builder")), ", ",
            typeName(elementType, kj::str("Reader")),
            ")(",
            dlangFullName(elementStructSchema, kj::str("")), ".newFactory(",
            kj::StringTree(
              KJ_MAP(arg, factoryArgs) {
                return kj::strTree(arg);
              }, ","),
            "))"
            ).flatten();
        } else {
          return kj::str(kj::strTree("cast(StructList.Factory!(", typeName(elementType, kj::str("Builder")), ", ", typeName(elementType, kj::str("Reader")), "))", typeName(elementType, kj::str("listFactory"))));
        }
      }
      case schema::Type::LIST:
        return kj::str("new ListList.Factory!(",
                       typeName(elementType, kj::str("Builder")),", ",
                       typeName(elementType, kj::str("Reader")), ")(",
                       makeFactoryArg(elementType),
                       ")");
      case schema::Type::ENUM:
        return kj::str("new EnumList.Factory!(",
                       typeName(elementType), ")(",
                       typeName(elementType, kj::str("")),
                       ".values())");
      default:
        return kj::str(kj::strTree("cast(", typeName(type, kj::str("Factory")), ")", typeName(type, kj::str("factory"))));
      }
    }
    default:
      KJ_UNREACHABLE;
    }

  }

  FieldText makeFieldText(kj::StringPtr scope, StructSchema::Field field, int indent) {
    auto proto = field.getProto();
    kj::String titleCase = toTitleCase(proto.getName());

    DiscriminantChecks unionDiscrim;
    if (hasDiscriminantValue(proto)) {
      unionDiscrim = makeDiscriminantChecks(scope, proto.getName(), field.getContainingStruct(), indent + 1);
    }

    switch (proto.which()) {
      case schema::Field::SLOT:
        // Continue below.
        break;

      case schema::Field::GROUP: {
        auto slots = getSortedSlots(schemaLoader.get(
            field.getProto().getGroup().getTypeId()).asStruct());
        return FieldText {
          kj::strTree(
            kj::mv(unionDiscrim.readerIsDef),
            spaces(indent+1), titleCase, ".Reader get", titleCase, "()\n", spaces(indent+1), "{\n",
            spaces(indent+2), "return ", scope, titleCase,
            ".Reader(b.segment, b.data, b.pointers, b.dataSize, b.pointerCount, b.nestingLimit);\n",
            spaces(indent+1), "}\n",
            spaces(indent+1), "\n"),

            kj::strTree(
              kj::mv(unionDiscrim.builderIsDef),
              spaces(indent+1), titleCase, ".Builder get", titleCase, "()\n", spaces(indent+1), "{\n",
              spaces(indent+2), "return ", scope, titleCase,
              ".Builder(b.segment, b.data, b.pointers, b.dataSize, b.pointerCount);\n",
              spaces(indent+1), "}\n",
              spaces(indent+1), titleCase, ".Builder init", titleCase, "()\n", spaces(indent+1), "{\n",
              unionDiscrim.set,
              KJ_MAP(slot, slots) {
                switch (sectionFor(slot.whichType)) {
                case Section::NONE:
                  return kj::strTree();
                case Section::DATA:
                  return kj::strTree(
                    spaces(indent+2),
                    "b._set", toTitleCase(maskType(slot.whichType)),
                    "Field(", slot.offset, ", ", maskZeroLiteral(slot.whichType),
                    ");\n");
                case Section::POINTERS:
                  return kj::strTree(
                    spaces(indent+2), "b._clearPointerField(", slot.offset, ");\n");
                }
                KJ_UNREACHABLE;
              },
              spaces(indent+2), "return ", scope, titleCase,
              ".Builder(b.segment, b.data, b.pointers, b.dataSize, b.pointerCount);\n",
              spaces(indent+1), "}\n",
              spaces(indent+1), "\n")
          };
      }
    }

    auto slot = proto.getSlot();

    FieldKind kind = FieldKind::PRIMITIVE;
    kj::String ownedType;
    kj::String builderType = typeName(field.getType()/*, kj::str("Builder")*/).flatten();
    kj::String readerType = typeName(field.getType()/*, kj::str("Reader")*/).flatten();
    kj::String defaultMask;    // primitives only
    size_t defaultOffset = 0;    // pointers only: offset of the default value within the schema.
    size_t defaultSize = 0;      // blobs only: byte size of the default value.

    auto typeBody = slot.getType();
    auto defaultBody = slot.getDefaultValue();
    switch (typeBody.which()) {
      case schema::Type::VOID:
        kind = FieldKind::PRIMITIVE;
        break;

#define HANDLE_PRIMITIVE(discrim, typeName, dlangTypeName, defaultName, suffix) \
      case schema::Type::discrim: \
        kind = FieldKind::PRIMITIVE; \
        if (defaultBody.get##defaultName() != 0) { \
          defaultMask = kj::str("cast(", #dlangTypeName, ")", kj::implicitCast< typeName>(defaultBody.get##defaultName()), #suffix); \
        } \
        break;

        HANDLE_PRIMITIVE(BOOL, bool, bool, Bool, );
        HANDLE_PRIMITIVE(INT8 , ::int8_t , byte, Int8 , );
        HANDLE_PRIMITIVE(INT16, ::int16_t, short, Int16, );
        HANDLE_PRIMITIVE(INT32, ::int32_t, int, Int32, );
        HANDLE_PRIMITIVE(INT64, ::int64_t, long, Int64, L);
        HANDLE_PRIMITIVE(UINT8 , ::uint8_t , ubyte, Uint8 , );
        HANDLE_PRIMITIVE(UINT16, ::uint16_t, ushort, Uint16, );
        HANDLE_PRIMITIVE(UINT32, ::uint32_t, uint, Uint32, );
        HANDLE_PRIMITIVE(UINT64, ::uint64_t, ulong, Uint64, UL);
#undef HANDLE_PRIMITIVE

      case schema::Type::FLOAT32:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getFloat32() != 0) {
          int32_t mask;
          float value = defaultBody.getFloat32();
          static_assert(sizeof(mask) == sizeof(value), "bug");
          memcpy(&mask, &value, sizeof(mask));
          defaultMask = kj::str(mask);
        }
        break;

      case schema::Type::FLOAT64:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getFloat64() != 0) {
          int64_t mask;
          double value = defaultBody.getFloat64();
          static_assert(sizeof(mask) == sizeof(value), "bug");
          memcpy(&mask, &value, sizeof(mask));
          defaultMask = kj::str(mask, "L");
        }
        break;

      case schema::Type::TEXT:
        kind = FieldKind::BLOB;
        if (defaultBody.hasText()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
          defaultSize = defaultBody.getText().size();
        }
        break;
      case schema::Type::DATA:
        kind = FieldKind::BLOB;
        if (defaultBody.hasData()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
          defaultSize = defaultBody.getData().size();
        }
        break;

      case schema::Type::ENUM:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getEnum() != 0) {
          defaultMask = kj::str("cast(ushort)", defaultBody.getEnum());
        }
        break;

      case schema::Type::STRUCT:
        kind = FieldKind::STRUCT;
        if (defaultBody.hasStruct()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        break;
      case schema::Type::LIST:
        kind = FieldKind::LIST;
        if (defaultBody.hasList()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        break;
      case schema::Type::INTERFACE:
        kind = FieldKind::INTERFACE;
        break;
      case schema::Type::ANY_POINTER:
        kind = FieldKind::ANY_POINTER;
        if (defaultBody.hasAnyPointer()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        break;
    }

    kj::String defaultMaskParam;
    if (defaultMask.size() > 0) {
      defaultMaskParam = kj::str(", ", defaultMask);
    }

    uint offset = slot.getOffset();

    auto structSchema = field.getContainingStruct();

    if (kind == FieldKind::PRIMITIVE) {
      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDef),
            spaces(indent+1), readerType, " get", titleCase, "()\n", spaces(indent+1), "{\n",
            unionDiscrim.check,
            (typeBody.which() == schema::Type::ENUM ?
             makeEnumGetter(field.getType().asEnum(),
                            offset, kj::str(defaultMaskParam), indent + 2) :
             (typeBody.which() == schema::Type::VOID ?
              kj::strTree(spaces(indent+2), "return Void.VOID;\n") :
              kj::strTree(spaces(indent+2), "return b._get",toTitleCase(readerType),"Field(", offset, defaultMaskParam, ");\n"))),
            spaces(indent+1), "}\n",
            spaces(indent+1), "\n"),

          kj::strTree(
            kj::mv(unionDiscrim.builderIsDef),
            spaces(indent+1), builderType, " get", titleCase, "()\n", spaces(indent+1), "{\n",
            unionDiscrim.check,
            (typeBody.which() == schema::Type::ENUM ?
             makeEnumGetter(field.getType().asEnum(),
                            offset, kj::str(defaultMaskParam), indent + 2) :
             (typeBody.which() == schema::Type::VOID ?
              kj::strTree(spaces(indent+2), "return Void.VOID;\n") :
              kj::strTree(spaces(indent+2), "return b._get", toTitleCase(builderType), "Field(", offset, defaultMaskParam, ");\n"))),
            spaces(indent+1), "}\n",

            typeBody.which() == schema::Type::VOID?
            strTree(spaces(indent+1), "void set", titleCase, "()\n", spaces(indent+1), "{\n")
            :
            strTree(spaces(indent+1), "void set", titleCase, "(", readerType, " value)\n", spaces(indent+1), "{\n"),
            unionDiscrim.set,
            (typeBody.which() == schema::Type::ENUM ?
             kj::strTree(spaces(indent+2), "b._setShortField(", offset, ", cast(ushort)value", defaultMaskParam, ");\n") :
             (typeBody.which() == schema::Type::VOID ?
              kj::strTree() :
              kj::strTree(spaces(indent+2), "b._set",
                          toTitleCase(builderType), "Field(", offset, ", value", defaultMaskParam, ");\n"))),
            spaces(indent+1), "}\n",
            spaces(indent+1), "\n")
      };

    } else if (kind == FieldKind::INTERFACE) {
      KJ_FAIL_REQUIRE("interfaces unimplemented");

    } else if (kind == FieldKind::ANY_POINTER) {

      auto factoryArg = makeFactoryArg(field.getType());

      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDef),
            spaces(indent+1), "bool has", titleCase, "()\n", spaces(indent+1), "{\n",
            unionDiscrim.has,
            spaces(indent+2), "return !b._pointerFieldIsNull(", offset, ");\n",
            spaces(indent+1), "}\n",

            spaces(indent+1), readerType, ".Reader get", titleCase, "()\n", spaces(indent+1), "{\n",
            unionDiscrim.check,
            spaces(indent+2), "return b._getPointerField!(", typeName(field.getType()), ")(", offset, ");\n",
            spaces(indent+1), "}\n",
            spaces(indent+1), "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDef),
            spaces(indent+1), "bool has", titleCase, "()\n", spaces(indent+1), "{\n",
            spaces(indent+2), "return !b._pointerFieldIsNull(", offset, ");\n",
            spaces(indent+1), "}\n",

            spaces(indent+1), builderType, ".Builder get", titleCase, "()\n", spaces(indent+1), "{\n",
            unionDiscrim.check,
            spaces(indent+2), "return b._getPointerField!(", typeName(field.getType()), ")(", offset, ");\n",
            spaces(indent+1), "}\n",

            spaces(indent+1), builderType, ".Builder init", titleCase, "()\n", spaces(indent+1), "{\n",
            unionDiscrim.set,
            spaces(indent+2), "return b._initPointerField!(", typeName(field.getType()), ")(", offset, ", 0);\n",
            spaces(indent+1), "}\n",

            spaces(indent+1), builderType, ".Builder init", titleCase, "(int size)\n", spaces(indent+1), "{\n",
            unionDiscrim.set,
            spaces(indent+2), "return b._initPointerField!(", typeName(field.getType()), ")(", offset, ", size);\n",
            spaces(indent+1), "}\n",

            (field.getType().getBrandParameter() == nullptr ? kj::strTree() :
             kj::strTree(spaces(indent+1), "void set", titleCase, "(", readerType, ".Reader value)\n", spaces(indent+1), "{\n",
                         unionDiscrim.set,
                         spaces(indent+2), "b._setPointerField!(", typeName(field.getType()), ")(", offset, ", value);\n",
                         spaces(indent+1), "}\n",
                         spaces(indent+1), "\n"))
        ),
      };

    } else if (kind == FieldKind::STRUCT) {
      uint64_t typeId = field.getContainingStruct().getProto().getId();
      kj::String defaultParams = defaultOffset == 0 ? kj::str("null, 0") : kj::str(
        "&Schemas.b_", kj::hex(typeId), ", ", defaultOffset);

      auto typeParamVec = getTypeParameters(field.getContainingStruct());
      auto factoryArg = makeFactoryArg(field.getType());

      bool isGeneric = false;
      {
        auto type = field.getType();
        while (type.isList()) {
          type = type.asList().getElementType();
        }

        if (type.isStruct()) {
          isGeneric = type.asStruct().getProto().getIsGeneric();
        }
      }

      return FieldText {
        kj::strTree(
          kj::mv(unionDiscrim.readerIsDef),
          spaces(indent+1), "bool has", titleCase, "()\n", spaces(indent+1), "{\n",
          spaces(indent+2), "return !b._pointerFieldIsNull(", offset, ");\n",
          spaces(indent+1), "}\n",

          spaces(indent+1), readerType, ".Reader get", titleCase, "()\n", spaces(indent+1), "{\n",
          unionDiscrim.check,
          spaces(indent+2), "return ",
          "b._getPointerField!(", typeName(field.getType()), ")(", offset,", ", defaultParams, ");\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), "\n"),

        kj::strTree(
          kj::mv(unionDiscrim.builderIsDef),
          spaces(indent+1), builderType, ".Builder get", titleCase, "()\n", spaces(indent+1), "{\n",
          unionDiscrim.check,
          spaces(indent+2), "return ",
          "b._getPointerField!(", builderType, ")(", offset, ", ", defaultParams, ");\n",
          spaces(indent+1), "}\n",

          (isGeneric?
          kj::strTree(
            spaces(indent+1), "void set", titleCase, "(T)(T value)\n", spaces(indent+1), "{\n",
            unionDiscrim.set,
            spaces(indent+2), "b._setPointerField!(", builderType, ")(", offset, ", value);\n",
            spaces(indent+1), "}\n"
          )
          :
          kj::strTree(
            spaces(indent+1), "void set", titleCase, "(", readerType, ".Reader value)\n", spaces(indent+1), "{\n",
            unionDiscrim.set,
            spaces(indent+2), "b._setPointerField!(", builderType, ")(", offset, ", value);\n",
            spaces(indent+1), "}\n"
          )),

          spaces(indent+1), builderType, ".Builder init", titleCase, "()\n", spaces(indent+1), "{\n",
          unionDiscrim.set,
          spaces(indent+2), "return ",
          "b._initPointerField!(", builderType, ")(", offset, ", 0);\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), "\n"),
      };

    } else if (kind == FieldKind::BLOB) {

      uint64_t typeId = field.getContainingStruct().getProto().getId();
      kj::String defaultParams = defaultOffset == 0 ? kj::str("null, 0, 0") : kj::str(
        "&Schemas.b_", kj::hex(typeId), ".buffer, ", defaultOffset, ", ", defaultSize);

      kj::String blobKind =  typeBody.which() == schema::Type::TEXT ? kj::str("Text") : kj::str("Data");
      kj::String setterInputType = typeBody.which() == schema::Type::TEXT ? kj::str("string") : kj::str("ubyte[]");

      return FieldText {
        kj::strTree(
          kj::mv(unionDiscrim.readerIsDef),
          spaces(indent+1), "bool has", titleCase, "()\n", spaces(indent+1), "{\n",
          unionDiscrim.has,
          spaces(indent+2), "return !b._pointerFieldIsNull(", offset, ");\n",
          spaces(indent+1), "}\n",

          spaces(indent+1), setterInputType,
          " get", titleCase, "()\n", spaces(indent+1), "{\n",
          spaces(indent+2), "return b._getPointerField!", blobKind, "(", offset, ", ", defaultParams, ")", (typeBody.which() == schema::Type::TEXT? ".toString()" : ".toArray()"), ";\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), "\n"),

        kj::strTree(
          kj::mv(unionDiscrim.builderIsDef),
          spaces(indent+1), "bool has", titleCase, "()\n", spaces(indent+1), "{\n",
          unionDiscrim.has,
          spaces(indent+2), "return !b._pointerFieldIsNull(", offset, ");\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), builderType, ".Builder get", titleCase, "()\n", spaces(indent+1), "{\n",
          spaces(indent+2), "return b._getPointerField!", blobKind, "(", offset, ", ", defaultParams, ");\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), "void set", titleCase, "(", readerType, ".Reader value)\n", spaces(indent+1), "{\n",
          unionDiscrim.set,
          spaces(indent+2), "b._setPointerField!", blobKind, "(", offset, ", value);\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), "void set", titleCase, "(", setterInputType, " value)\n", spaces(indent+1), "{\n",
          unionDiscrim.set,
          spaces(indent+2), "b._setPointerField!", blobKind, "(", offset, ", ",
          readerType, ".Reader(value));\n",
          spaces(indent+1), "}\n",

          spaces(indent+1), builderType, ".Builder init", titleCase, "(int size)\n", spaces(indent+1), "{\n",
          unionDiscrim.set,
          spaces(indent+2), "return b._initPointerField!", blobKind, "(", offset, ", size);\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), "\n"),
      };
    } else if (kind == FieldKind::LIST) {

      uint64_t typeId = field.getContainingStruct().getProto().getId();
      kj::String defaultParams = defaultOffset == 0 ? kj::str("null, 0") : kj::str(
        "&Schemas.b_", kj::hex(typeId), ", ", defaultOffset);

      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDef),
            spaces(indent+1), "bool has", titleCase, "()\n", spaces(indent+1), "{\n",
            spaces(indent+2), "return !b._pointerFieldIsNull(", offset, ");\n",
            spaces(indent+1), "}\n",

             kj::strTree(
               spaces(indent+1), readerType,
               ".Reader get", titleCase, "()\n", spaces(indent+1), "{\n",
               spaces(indent+2), "return ", kj::str("b._getPointerField!(", typeName(field.getType())), ")(", offset, ", ", defaultParams, ");\n",
               spaces(indent+1), "}\n"
            ),
            spaces(indent+1), "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDef),
            spaces(indent+1), "bool has", titleCase, "()\n", spaces(indent+1), "{\n",
            spaces(indent+2), "return !b._pointerFieldIsNull(", offset, ");\n",
            spaces(indent+1), "}\n",

            kj::strTree(
               spaces(indent+1), builderType,
               ".Builder get", titleCase, "()\n", spaces(indent+1), "{\n",
               spaces(indent+2), "return ", kj::str("b._getPointerField!(", typeName(field.getType())), ")(", offset, ", ", defaultParams, ");\n",
               spaces(indent+1), "}\n"
            ),

            kj::strTree(
               spaces(indent+1), "void set", titleCase, "(", readerType, ".Reader value)\n", spaces(indent+1), "{\n",
               unionDiscrim.set,
               spaces(indent+2), kj::str("b._setPointerField!(", typeName(field.getType())), ")(", offset, ", value);\n",
               spaces(indent+1), "}\n"
            ),

            kj::strTree(
              spaces(indent+1), builderType,
              ".Builder init", titleCase, "(int size)\n", spaces(indent+1), "{\n",
              unionDiscrim.set,
              spaces(indent+2), "return ", kj::str("b._initPointerField!(", typeName(field.getType())), ")(", offset, ", size);\n",
              spaces(indent+1), "}\n",
              spaces(indent+1), "\n")
            ),
      };
    } else {
      KJ_UNREACHABLE;
    }
  }

  // -----------------------------------------------------------------

  struct StructText {
    kj::StringTree outerTypeDef;
    kj::StringTree readerBuilderDefs;
    kj::StringTree inlineMethodDefs;
  };

  kj::StringTree makeWhich(StructSchema schema, int indent) {
    if (schema.getProto().getStruct().getDiscriminantCount() == 0) {
      return kj::strTree();
    } else {
      auto fields = schema.getUnionFields();
      return kj::strTree(
        spaces(indent), "Which which()\n", spaces(indent), "{\n",
        spaces(indent+1), "switch(b._getUshortField(",
        schema.getProto().getStruct().getDiscriminantOffset(), "))\n", spaces(indent+1), "{\n",
        KJ_MAP(f, fields) {
          return kj::strTree(spaces(indent+2), "case ", f.getProto().getDiscriminantValue(), ": return ",
                             "Which.", safeIdentifier(f.getProto().getName()), ";\n");
        },
        spaces(indent+2), "default: return Which._NOT_IN_SCHEMA;\n",
        spaces(indent+1), "}\n",
        spaces(indent), "}\n",
        spaces(indent), "\n"
        );
    }
  }

  StructText makeStructText(kj::StringPtr scope, kj::StringPtr name, StructSchema schema,
                            kj::Array<kj::StringTree> nestedTypeDecls, int indent) {
    auto proto = schema.getProto();
    auto fullName = kj::str(scope, name);
    auto subScope = kj::str(fullName, ".");
    auto fieldTexts = KJ_MAP(f, schema.getFields()) { return makeFieldText(subScope, f, indent + 1); };

    auto structNode = proto.getStruct();
    uint discrimOffset = structNode.getDiscriminantOffset();
    structNode.getPointerCount();

    auto typeParamVec = getTypeParameters(schema);
    bool hasTypeParams = typeParamVec.size() > 0;

    kj::StringTree typeParamsTree;
    if (hasTypeParams) {
      typeParamsTree = kj::strTree(
        "(",
        kj::StringTree(KJ_MAP(p, typeParamVec) {
            return kj::strTree(p, "_Type");
          }, ", "),
        ")");
    }
    kj::String typeParams = typeParamsTree.flatten();

    return StructText {
      kj::strTree(
        spaces(indent), "struct ", name, typeParams, "\n", spaces(indent), "{\n", spaces(indent), "public:\n",
        kj::strTree(
          spaces(indent+1), "static immutable structSize =",
          " cast(immutable)StructSize(", structNode.getDataWordCount(), ", ", structNode.getPointerCount(), ");\n", spaces(indent+1), "\n"),

        kj::strTree(
          spaces(indent+1), "static struct Builder\n", spaces(indent+1), "{\n", spaces(indent+1), "public:\n",
          spaces(indent+2), "this(SegmentBuilder* segment, int data, int pointers, int dataSize, short pointerCount)\n", spaces(indent+2), "{\n",
          spaces(indent+3), "b = StructBuilder(segment, data, pointers, dataSize, pointerCount);\n",
          spaces(indent+2), "}\n",
          spaces(indent+2), "\n",
          makeWhich(schema, indent+2),
          spaces(indent+2), "\n",
          spaces(indent+2), "auto asReader()\n",
          spaces(indent+2), "{\n",
          spaces(indent+3), "return b.asReader!Reader();\n",
          spaces(indent+2), "}\n",
          spaces(indent+2), "\n",
          KJ_MAP(f, fieldTexts) { return kj::mv(f.builderMethodDecls); },
          spaces(indent+1), "public:\n",
          spaces(indent+2), "StructBuilder b;\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), "\n"),

        kj::strTree(
          spaces(indent+1), "static struct Reader\n", spaces(indent+1), "{\n", spaces(indent+1), "public:\n",
          spaces(indent+2), "this(SegmentReader* segment, int data, int pointers, int dataSize, short pointerCount, int nestingLimit)\n", spaces(indent+2), "{\n",
          spaces(indent+3), "b = StructReader(segment, data, pointers, dataSize, pointerCount, nestingLimit);\n",
          spaces(indent+2), "}\n",
          spaces(indent+2), "\n",
          makeWhich(schema, indent+2),
          KJ_MAP(f, fieldTexts) { return kj::mv(f.readerMethodDecls); },
          spaces(indent+1), "public:\n",
          spaces(indent+2), "StructReader b;\n",
          spaces(indent+1), "}\n",
          spaces(indent+1), "\n"),

        structNode.getDiscriminantCount() == 0 ?
        kj::strTree() :
        kj::strTree(
          spaces(indent+1), "enum Which : ushort\n", spaces(indent+1), "{\n",
          KJ_MAP(f, structNode.getFields()) {
            if (hasDiscriminantValue(f)) {
              return kj::strTree(spaces(indent+2), safeIdentifier(f.getName()), ",\n");
            } else {
              return kj::strTree();
            }
          },
          spaces(indent+2), "_NOT_IN_SCHEMA,\n",
          spaces(indent+1), "}\n"),
        KJ_MAP(n, nestedTypeDecls) { return kj::mv(n); },
        spaces(indent), "}\n",
        spaces(indent), "\n"),

        kj::strTree(),
        kj::strTree()
        };
  }


  // -----------------------------------------------------------------

  struct ConstText {
    bool needsSchema;
    kj::StringTree decl;
  };

  ConstText makeConstText(kj::StringPtr scope, kj::StringPtr name, ConstSchema schema, int indent) {
    auto proto = schema.getProto();
    auto constProto = proto.getConst();
    auto type = schema.getType();
    auto typeName_ = typeName(type, kj::str("Reader")).flatten();
    auto upperCase = name;

    switch (type.which()) {
      case schema::Value::VOID:
      case schema::Value::BOOL:
      case schema::Value::INT8:
      case schema::Value::INT16:
      case schema::Value::INT32:
      case schema::Value::INT64:
      case schema::Value::UINT8:
      case schema::Value::UINT16:
      case schema::Value::UINT32:
      case schema::Value::UINT64:
      case schema::Value::FLOAT32:
      case schema::Value::FLOAT64:
      case schema::Value::ENUM:
        return ConstText {
          false,
            kj::strTree(spaces(indent), "static const ", typeName_, ' ', upperCase, " = ",
                        literalValue(constProto.getType(), constProto.getValue()), ";\n")
        };

      case schema::Value::TEXT: {
        return ConstText {
          true,
          kj::strTree(spaces(indent),
                      "static const ", upperCase,
                      " = GeneratedClassSupport.Const!Text(&Schemas.b_", kj::hex(proto.getId()), ", ",
                      schema.getValueSchemaOffset(), ", ",
                      constProto.getValue().getText().size(), ");\n")
        };
      }

      case schema::Value::DATA: {
        return ConstText {
          true,
          kj::strTree(spaces(indent),
                      "static const ", upperCase,
                      " = GeneratedClassSupport.Const!Data(&Schemas.b_", kj::hex(proto.getId()), ", ",
                      schema.getValueSchemaOffset(), ", ",
                      constProto.getValue().getData().size(), ");\n")
        };
      }

      case schema::Value::STRUCT: {
        return ConstText {
          true,
            kj::strTree(spaces(indent),
                        "static const ", upperCase, " = GeneratedClassSupport.Const!(", typeName(type), ")(&Schemas.b_",
                        kj::hex(proto.getId()), ", ", schema.getValueSchemaOffset(), ", 0x7fffffff);\n")
        };
      }

      case schema::Value::LIST: {
        return ConstText {
          true,
          kj::strTree(
            spaces(indent),
            "static const ", upperCase, " = ",
            "GeneratedClassSupport.Const!(", typeName(type), ")(&Schemas.b_",
            kj::hex(proto.getId()), ", ", schema.getValueSchemaOffset(), ", 0x7fffffff);\n")
        };
      }

      case schema::Value::ANY_POINTER:
      case schema::Value::INTERFACE:
        return ConstText { false, kj::strTree() };
    }

    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------

  struct NodeText {
    kj::StringTree outerTypeDef;
    kj::StringTree readerBuilderDefs;
    kj::StringTree inlineMethodDefs;
    kj::StringTree capnpSchemaDefs;
    kj::StringTree capnpPrivateDefs;
    kj::StringTree sourceFileDefs;
  };

  struct NodeTextNoSchema {
    kj::StringTree outerTypeDef;
    kj::StringTree readerBuilderDefs;
    kj::StringTree inlineMethodDefs;
    kj::StringTree capnpPrivateDefs;
    kj::StringTree sourceFileDefs;
  };

  NodeText makeNodeText(kj::StringPtr scope,
                        kj::StringPtr name, Schema schema,
                        int indent) {
    auto proto = schema.getProto();
    auto fullName = kj::str(scope, name);
    auto subScope = kj::str(fullName, ".");
    auto hexId = kj::hex(proto.getId());

    // Compute nested nodes, including groups.
    kj::Vector<NodeText> nestedTexts(proto.getNestedNodes().size());
    for (auto nested: proto.getNestedNodes()) {
      nestedTexts.add(makeNodeText(
                                   subScope, nested.getName(), schemaLoader.getUnbound(nested.getId()), indent + 1));
    };

    if (proto.isStruct()) {
      for (auto field: proto.getStruct().getFields()) {
        if (field.isGroup()) {
          nestedTexts.add(makeNodeText(subScope, toTitleCase(field.getName()),
              schemaLoader.getUnbound(field.getGroup().getTypeId()), indent + 1));
        }
      }
    } else if (proto.isInterface()) {
      KJ_FAIL_REQUIRE("interfaces not implemented");
    }

    // Convert the encoded schema to a literal byte array.
    kj::ArrayPtr<const word> rawSchema = schema.asUncheckedMessage();
    auto schemaLiteral = kj::StringTree(KJ_MAP(w, rawSchema) {
      const byte* bytes = reinterpret_cast<const byte*>(&w);

      return kj::strTree(
        spaces(1), "  ",
        KJ_MAP(i, kj::range<uint>(0, sizeof(word))) {
          auto text = kj::hex(bytes[i]);
          return kj::strTree("0x", text, ",");
        });
    }, "\n");

    std::set<uint64_t> deps;
    enumerateDeps(proto, deps);

    kj::Array<uint> membersByName;
    kj::Array<uint> membersByDiscrim;
    switch (proto.which()) {
      case schema::Node::STRUCT: {
        auto structSchema = schema.asStruct();
        membersByName = makeMembersByName(structSchema.getFields());
        auto builder = kj::heapArrayBuilder<uint>(structSchema.getFields().size());
        for (auto field: structSchema.getUnionFields()) {
          builder.add(field.getIndex());
        }
        for (auto field: structSchema.getNonUnionFields()) {
          builder.add(field.getIndex());
        }
        membersByDiscrim = builder.finish();
        break;
      }
      case schema::Node::ENUM:
        membersByName = makeMembersByName(schema.asEnum().getEnumerants());
        break;
      case schema::Node::INTERFACE:
        membersByName = makeMembersByName(schema.asInterface().getMethods());
        break;
      default:
        break;
    }

    auto schemaDef = kj::strTree(
      spaces(1), "__gshared static SegmentReader b_", hexId, " = GeneratedClassSupport.decodeRawBytes([\n",
      kj::mv(schemaLiteral), "\n",
      spaces(1), "]);\n");
/*
        deps.size() == 0 ? kj::strTree() : kj::strTree(
            "static const ::capnp::_::RawSchema* const d_", hexId, "[] = {\n",
            KJ_MAP(depId, deps) {
              return kj::strTree("  &s_", kj::hex(depId), ",\n");
            },
            "};\n"),
        membersByName.size() == 0 ? kj::strTree() : kj::strTree(
            "static const uint16_t m_", hexId, "[] = {",
            kj::StringTree(KJ_MAP(index, membersByName) { return kj::strTree(index); }, ", "),
            "};\n"),
        membersByDiscrim.size() == 0 ? kj::strTree() : kj::strTree(
            "static const uint16_t i_", hexId, "[] = {",
            kj::StringTree(KJ_MAP(index, membersByDiscrim) { return kj::strTree(index); }, ", "),
            "};\n"),
        "const ::capnp::_::RawSchema s_", hexId, " = {\n"
        "  0x", hexId, ", b_", hexId, ".words, ", rawSchema.size(), ", ",
        deps.size() == 0 ? kj::strTree("nullptr") : kj::strTree("d_", hexId), ", ",
        membersByName.size() == 0 ? kj::strTree("nullptr") : kj::strTree("m_", hexId), ",\n",
        "  ", deps.size(), ", ", membersByName.size(), ", ",
        membersByDiscrim.size() == 0 ? kj::strTree("nullptr") : kj::strTree("i_", hexId),
        ", nullptr, nullptr\n"
        "};\n");*/

    NodeTextNoSchema top = makeNodeTextWithoutNested(
        scope, name, schema,
        KJ_MAP(n, nestedTexts) { return kj::mv(n.outerTypeDef); }, indent);

    return NodeText {
      kj::strTree(
          kj::mv(top.outerTypeDef),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.outerTypeDef); }),

      kj::strTree(
          kj::mv(top.readerBuilderDefs),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.readerBuilderDefs); }),

      kj::strTree(
          kj::mv(top.inlineMethodDefs),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.inlineMethodDefs); }),

      kj::strTree(
          kj::mv(schemaDef),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.capnpSchemaDefs); }),

      kj::strTree(
          kj::mv(top.capnpPrivateDefs),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.capnpPrivateDefs); }),

      kj::strTree(
          kj::mv(top.sourceFileDefs),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.sourceFileDefs); }),
    };
  }

  NodeTextNoSchema makeNodeTextWithoutNested(kj::StringPtr scope,
                                             kj::StringPtr name, Schema schema,
                                             kj::Array<kj::StringTree> nestedTypeDecls,
                                             int indent) {
    auto proto = schema.getProto();
    auto fullName = kj::str(scope, name);
    auto hexId = kj::hex(proto.getId());

    switch (proto.which()) {
      case schema::Node::FILE:
        KJ_FAIL_REQUIRE("This method shouldn't be called on file nodes.");

      case schema::Node::STRUCT: {
        StructText structText =
          makeStructText(scope, name, schema.asStruct(), kj::mv(nestedTypeDecls), indent);
        auto structNode = proto.getStruct();

        return NodeTextNoSchema {
          kj::mv(structText.outerTypeDef),
          kj::mv(structText.readerBuilderDefs),
          kj::mv(structText.inlineMethodDefs),

          kj::strTree(),
          kj::strTree(),
        };
      }

      case schema::Node::ENUM: {
        auto enumerants = schema.asEnum().getEnumerants();

        return NodeTextNoSchema {
          kj::strTree(
            spaces(indent), "enum ", name, " : ushort\n", spaces(indent), "{\n",
            KJ_MAP(e, enumerants) {
              return kj::strTree(spaces(indent+1), safeIdentifier(e.getProto().getName()), ",\n");
            },
            spaces(indent+1), "_NOT_IN_SCHEMA,\n",
            spaces(indent), "}\n",
            spaces(indent), "\n"),

          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
        };
      }

      case schema::Node::INTERFACE: {
        hasInterfaces = true;
        KJ_FAIL_REQUIRE("unimplemented");
      }

      case schema::Node::CONST: {
        auto constText = makeConstText(scope, name, schema.asConst(), indent);

        return NodeTextNoSchema {
          kj::strTree(kj::mv(constText.decl)),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
        };
      }

      case schema::Node::ANNOTATION: {
        return NodeTextNoSchema {
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
        };
      }
    }

    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------

  struct FileText {
    kj::StringPtr module;
    kj::StringTree source;
  };

  FileText makeFileText(Schema schema, capnp::List<::capnp::schema::Node>::Reader nodes,
                        schema::CodeGeneratorRequest::RequestedFile::Reader request) {
    usedImports.clear();

    auto node = schema.getProto();
    auto displayName = node.getDisplayName();

    kj::StringPtr moduleName;

    for (auto annotation: node.getAnnotations()) {
      if (annotation.getId() == MODULE_ANNOTATION_ID) {
        moduleName = annotation.getValue().getText();
      }
    }

    if (moduleName.size() == 0) {
      context.exitError(kj::str(displayName, ": no D module name found. See dlang.capnp."));
    }

    auto nodeTexts = KJ_MAP(nested, node.getNestedNodes()) {
      return makeNodeText("", nested.getName(), schemaLoader.getUnbound(nested.getId()), 0);
    };

    kj::String separator = kj::str("//", kj::repeat('=', 87), "\n");

    kj::Vector<kj::StringPtr> imports;
    for (auto import: request.getImports()) {
      if (usedImports.count(import.getId()) > 0) {
        auto proto = schemaLoader.get(import.getId()).getProto();
        for (auto annotation: proto.getAnnotations()) {
          if (annotation.getId() == MODULE_ANNOTATION_ID) {
            imports.add(annotation.getValue().getText());
          }
        }
      }
    }

    kj::StringTree sourceDefs = kj::strTree(
        KJ_MAP(n, nodeTexts) { return kj::mv(n.sourceFileDefs); });

    return FileText {
      moduleName,
      kj::strTree(
          "//Generated by Cap'n Proto compiler, DO NOT EDIT.\n"
          "//source: ", baseName(displayName), "\n\n",
          "module ", moduleName, ";\n\n",
          "import capnproto;\n\n",
          KJ_MAP(n, imports) { return kj::strTree("import ", n, ";\n\n"); },
          KJ_MAP(n, nodeTexts) { return kj::mv(n.outerTypeDef); },
          "struct Schemas\n{\npublic:\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.capnpSchemaDefs); },
          "}\n",
          "\n")
    };
  }

  // -----------------------------------------------------------------

  void makeDirectory(kj::StringPtr path) {
    KJ_IF_MAYBE(slashpos, path.findLast('/')) {
      // Make the parent dir.
      makeDirectory(kj::str(path.slice(0, *slashpos)));
    }

    if (mkdir(path.cStr(), 0777) < 0) {
      int error = errno;
      if (error != EEXIST) {
        KJ_FAIL_SYSCALL("mkdir(path)", error, path);
      }
    }
  }

  void writeFile(kj::StringPtr filename, const kj::StringTree& text) {
    if (!filename.startsWith("/")) {
      KJ_IF_MAYBE(slashpos, filename.findLast('/')) {
        // Make the parent dir.
        makeDirectory(kj::str(filename.slice(0, *slashpos)));
      }
    }

    int fd;
    KJ_SYSCALL(fd = open(filename.cStr(), O_CREAT | O_WRONLY | O_TRUNC, 0666), filename);
    kj::FdOutputStream out((kj::AutoCloseFd(fd)));

    text.visit(
        [&](kj::ArrayPtr<const char> text) {
          out.write(text.begin(), text.size());
        });
  }

  kj::MainBuilder::Validity run() {
    ReaderOptions options;
    options.traversalLimitInWords = 1 << 30;  // Don't limit.
    StreamFdMessageReader reader(STDIN_FILENO, options);
    auto request = reader.getRoot<schema::CodeGeneratorRequest>();

    for (auto node: request.getNodes()) {
      schemaLoader.load(node);
    }

    kj::FdOutputStream rawOut(STDOUT_FILENO);
    kj::BufferedOutputStreamWrapper out(rawOut);

    for (auto requestedFile: request.getRequestedFiles()) {
      auto schema = schemaLoader.get(requestedFile.getId());

      auto fileText = makeFileText(schema, request.getNodes(), requestedFile);
      auto filename = fileText.module;
      size_t stemend = 0;
      KJ_IF_MAYBE(dotpos, filename.findLast('.')) {
        stemend = *dotpos + 1;
      }

      writeFile(kj::str(filename.slice(stemend), ".d"), fileText.source);
    }

    return true;
  }
};

}  // namespace
}  // namespace capnp

KJ_MAIN(capnp::CapnpcDlangMain);
