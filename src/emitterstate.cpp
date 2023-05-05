#include <limits>

#include "emitterstate.h"
#include "yaml-cpp/exceptions.h"  // IWYU pragma: keep

namespace YAML {
EmitterState::EmitterState()
    : m_isGood(true),
      m_lastError{},
      // default global manipulators
      m_charset(EMITTER_MANIP::EmitNonAscii),
      m_strFmt(EMITTER_MANIP::Auto),
      m_boolFmt(EMITTER_MANIP::TrueFalseBool),
      m_boolLengthFmt(EMITTER_MANIP::LongBool),
      m_boolCaseFmt(EMITTER_MANIP::LowerCase),
      m_nullFmt(EMITTER_MANIP::TildeNull),
      m_intFmt(EMITTER_MANIP::Dec),
      m_indent(2),
      m_preCommentIndent(2),
      m_postCommentIndent(1),
      m_seqFmt(EMITTER_MANIP::Block),
      m_mapFmt(EMITTER_MANIP::Block),
      m_mapKeyFmt(EMITTER_MANIP::Auto),
      m_floatPrecision(std::numeric_limits<float>::max_digits10),
      m_doublePrecision(std::numeric_limits<double>::max_digits10),
      //
      m_modifiedSettings{},
      m_globalModifiedSettings{},
      m_groups{},
      m_curIndent(0),
      m_hasAnchor(false),
      m_hasAlias(false),
      m_hasTag(false),
      m_hasNonContent(false),
      m_docCount(0) {}

EmitterState::~EmitterState() = default;

// SetLocalValue
// . We blindly tries to set all possible formatters to this value
// . Only the ones that make sense will be accepted
void EmitterState::SetLocalValue(EMITTER_MANIP value) {
  SetOutputCharset(value, FmtScope::value::Local);
  SetStringFormat(value, FmtScope::value::Local);
  SetBoolFormat(value, FmtScope::value::Local);
  SetBoolCaseFormat(value, FmtScope::value::Local);
  SetBoolLengthFormat(value, FmtScope::value::Local);
  SetNullFormat(value, FmtScope::value::Local);
  SetIntFormat(value, FmtScope::value::Local);
  SetFlowType(GroupType::value::Seq, value, FmtScope::value::Local);
  SetFlowType(GroupType::value::Map, value, FmtScope::value::Local);
  SetMapKeyFormat(value, FmtScope::value::Local);
}

void EmitterState::SetAnchor() { m_hasAnchor = true; }

void EmitterState::SetAlias() { m_hasAlias = true; }

void EmitterState::SetTag() { m_hasTag = true; }

void EmitterState::SetNonContent() { m_hasNonContent = true; }

void EmitterState::SetLongKey() {
  assert(!m_groups.empty());
  if (m_groups.empty()) {
    return;
  }

  assert(m_groups.back()->type == GroupType::value::Map);
  m_groups.back()->longKey = true;
}

void EmitterState::ForceFlow() {
  assert(!m_groups.empty());
  if (m_groups.empty()) {
    return;
  }

  m_groups.back()->flowType = FlowType::value::Flow;
}

void EmitterState::StartedNode() {
  if (m_groups.empty()) {
    m_docCount++;
  } else {
    m_groups.back()->childCount++;
    if (m_groups.back()->childCount % 2 == 0) {
      m_groups.back()->longKey = false;
    }
  }

  m_hasAnchor = false;
  m_hasAlias = false;
  m_hasTag = false;
  m_hasNonContent = false;
}

EmitterNodeType::value EmitterState::NextGroupType(
    GroupType::value type) const {
  if (type == GroupType::value::Seq) {
    if (GetFlowType(type) == EMITTER_MANIP::Block)
      return EmitterNodeType::BlockSeq;
    return EmitterNodeType::FlowSeq;
  }

  if (GetFlowType(type) == EMITTER_MANIP::Block)
    return EmitterNodeType::BlockMap;
  return EmitterNodeType::FlowMap;

  // can't happen
  assert(false);
  return EmitterNodeType::NoType;
}

void EmitterState::StartedDoc() {
  m_hasAnchor = false;
  m_hasTag = false;
  m_hasNonContent = false;
}

void EmitterState::EndedDoc() {
  m_hasAnchor = false;
  m_hasTag = false;
  m_hasNonContent = false;
}

void EmitterState::StartedScalar() {
  StartedNode();
  ClearModifiedSettings();
}

void EmitterState::StartedGroup(GroupType::value type) {
  StartedNode();

  const std::size_t lastGroupIndent =
      (m_groups.empty() ? 0 : m_groups.back()->indent);
  m_curIndent += lastGroupIndent;

  // TODO: Create move constructors for settings types to simplify transfer
  std::unique_ptr<Group> pGroup(new Group(type));

  // transfer settings (which last until this group is done)
  //
  // NB: if pGroup->modifiedSettings == m_modifiedSettings,
  // m_modifiedSettings is not changed!
  pGroup->modifiedSettings = std::move(m_modifiedSettings);

  // set up group
  if (GetFlowType(type) == EMITTER_MANIP::Block) {
    pGroup->flowType = FlowType::value::Block;
  } else {
    pGroup->flowType = FlowType::value::Flow;
  }
  pGroup->indent = GetIndent();

  m_groups.push_back(std::move(pGroup));
}

void EmitterState::EndedGroup(GroupType::value type) {
  if (m_groups.empty()) {
    if (type == GroupType::value::Seq) {
      return SetError(ErrorMsg::UNEXPECTED_END_SEQ);
    }
    return SetError(ErrorMsg::UNEXPECTED_END_MAP);
  }

  if (m_hasTag) {
    SetError(ErrorMsg::INVALID_TAG);
  }
  if (m_hasAnchor) {
    SetError(ErrorMsg::INVALID_ANCHOR);
  }

  // get rid of the current group
  {
    std::unique_ptr<Group> pFinishedGroup = std::move(m_groups.back());
    m_groups.pop_back();
    if (pFinishedGroup->type != type) {
      return SetError(ErrorMsg::UNMATCHED_GROUP_TAG);
    }
  }

  // reset old settings
  std::size_t lastIndent = (m_groups.empty() ? 0 : m_groups.back()->indent);
  assert(m_curIndent >= lastIndent);
  m_curIndent -= lastIndent;

  // some global settings that we changed may have been overridden
  // by a local setting we just popped, so we need to restore them
  m_globalModifiedSettings.restore();

  ClearModifiedSettings();
  m_hasAnchor = false;
  m_hasTag = false;
  m_hasNonContent = false;
}

EmitterNodeType::value EmitterState::CurGroupNodeType() const {
  if (m_groups.empty()) {
    return EmitterNodeType::NoType;
  }

  return m_groups.back()->NodeType();
}

GroupType::value EmitterState::CurGroupType() const {
  return m_groups.empty() ? GroupType::value::NoType : m_groups.back()->type;
}

FlowType::value EmitterState::CurGroupFlowType() const {
  return m_groups.empty() ? FlowType::value::NoType : m_groups.back()->flowType;
}

std::size_t EmitterState::CurGroupIndent() const {
  return m_groups.empty() ? 0 : m_groups.back()->indent;
}

std::size_t EmitterState::CurGroupChildCount() const {
  return m_groups.empty() ? m_docCount : m_groups.back()->childCount;
}

bool EmitterState::CurGroupLongKey() const {
  return m_groups.empty() ? false : m_groups.back()->longKey;
}

std::size_t EmitterState::LastIndent() const {
  if (m_groups.size() <= 1) {
    return 0;
  }

  return m_curIndent - m_groups[m_groups.size() - 2]->indent;
}

void EmitterState::ClearModifiedSettings() { m_modifiedSettings.clear(); }

void EmitterState::RestoreGlobalModifiedSettings() {
  m_globalModifiedSettings.restore();
}

bool EmitterState::SetOutputCharset(EMITTER_MANIP value,
                                    FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::EmitNonAscii:
    case EMITTER_MANIP::EscapeNonAscii:
    case EMITTER_MANIP::EscapeAsJson:
      _Set(m_charset, value, scope);
      return true;
    default:
      return false;
  }
}

bool EmitterState::SetStringFormat(EMITTER_MANIP value, FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::Auto:
    case EMITTER_MANIP::SingleQuoted:
    case EMITTER_MANIP::DoubleQuoted:
    case EMITTER_MANIP::Literal:
      _Set(m_strFmt, value, scope);
      return true;
    default:
      return false;
  }
}

bool EmitterState::SetBoolFormat(EMITTER_MANIP value, FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::OnOffBool:
    case EMITTER_MANIP::TrueFalseBool:
    case EMITTER_MANIP::YesNoBool:
      _Set(m_boolFmt, value, scope);
      return true;
    default:
      return false;
  }
}

bool EmitterState::SetBoolLengthFormat(EMITTER_MANIP value,
                                       FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::LongBool:
    case EMITTER_MANIP::ShortBool:
      _Set(m_boolLengthFmt, value, scope);
      return true;
    default:
      return false;
  }
}

bool EmitterState::SetBoolCaseFormat(EMITTER_MANIP value,
                                     FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::UpperCase:
    case EMITTER_MANIP::LowerCase:
    case EMITTER_MANIP::CamelCase:
      _Set(m_boolCaseFmt, value, scope);
      return true;
    default:
      return false;
  }
}

bool EmitterState::SetNullFormat(EMITTER_MANIP value, FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::LowerNull:
    case EMITTER_MANIP::UpperNull:
    case EMITTER_MANIP::CamelNull:
    case EMITTER_MANIP::TildeNull:
      _Set(m_nullFmt, value, scope);
      return true;
    default:
      return false;
  }
}

bool EmitterState::SetIntFormat(EMITTER_MANIP value, FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::Dec:
    case EMITTER_MANIP::Hex:
    case EMITTER_MANIP::Oct:
      _Set(m_intFmt, value, scope);
      return true;
    default:
      return false;
  }
}

bool EmitterState::SetIndent(std::size_t value, FmtScope::value scope) {
  if (value <= 1)
    return false;

  _Set(m_indent, value, scope);
  return true;
}

bool EmitterState::SetPreCommentIndent(std::size_t value,
                                       FmtScope::value scope) {
  if (value == 0)
    return false;

  _Set(m_preCommentIndent, value, scope);
  return true;
}

bool EmitterState::SetPostCommentIndent(std::size_t value,
                                        FmtScope::value scope) {
  if (value == 0)
    return false;

  _Set(m_postCommentIndent, value, scope);
  return true;
}

bool EmitterState::SetFlowType(GroupType::value groupType, EMITTER_MANIP value,
                               FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::Block:
    case EMITTER_MANIP::Flow:
      _Set(groupType == GroupType::value::Seq ? m_seqFmt : m_mapFmt, value, scope);
      return true;
    default:
      return false;
  }
}

EMITTER_MANIP EmitterState::GetFlowType(GroupType::value groupType) const {
  // force flow style if we're currently in a flow
  if (CurGroupFlowType() == FlowType::value::Flow)
    return EMITTER_MANIP::Flow;

  // otherwise, go with what's asked of us
  return (groupType == GroupType::value::Seq ? m_seqFmt.get() : m_mapFmt.get());
}

bool EmitterState::SetMapKeyFormat(EMITTER_MANIP value, FmtScope::value scope) {
  switch (value) {
    case EMITTER_MANIP::Auto:
    case EMITTER_MANIP::LongKey:
      _Set(m_mapKeyFmt, value, scope);
      return true;
    default:
      return false;
  }
}

bool EmitterState::SetFloatPrecision(std::size_t value, FmtScope::value scope) {
  if (value > std::numeric_limits<float>::max_digits10)
    return false;
  _Set(m_floatPrecision, value, scope);
  return true;
}

bool EmitterState::SetDoublePrecision(std::size_t value,
                                      FmtScope::value scope) {
  if (value > std::numeric_limits<double>::max_digits10)
    return false;
  _Set(m_doublePrecision, value, scope);
  return true;
}
}  // namespace YAML
