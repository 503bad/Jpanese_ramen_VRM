#include "rvrm/avatar.h"

namespace rvrm {

bool Avatar::loadFromFile(const std::string& utf8Path, const AvatarOptions& options, std::string* error) {
    bound_ = false;
    if (!loadVrmFromFile(utf8Path, model_, error, options.load)) return false;
    return finishLoad(options);
}

bool Avatar::loadFromMemory(const void* data, size_t size, const AvatarOptions& options, std::string* error) {
    bound_ = false;
    if (!loadVrmFromMemory(data, size, model_, error, options.load)) return false;
    return finishLoad(options);
}

bool Avatar::finishLoad(const AvatarOptions& options) {
    options_ = options;
    convertModel(model_, options.convert);
    skeleton_.init(model_);
    skinner_.init(model_);
    pose_ = skeleton_.restPose();
    expressionWeights_.assign(model_.expressions.size(), 0.0f);
    evaluateExpressions(model_, expressionWeights_, morphWeights_);
    skinner_.skin(model_, pose_, morphWeights_, skinned_);
    return true;
}

bool Avatar::bindSource(const SourcePose& sourceRest, std::string* error) {
    bound_ = retargeter_.init(model_, skeleton_, sourceRest, options_.retarget, error);
    if (bound_ && options_.enableSpringBones) springs_.init(model_, skeleton_, options_.spring);
    return bound_;
}

void Avatar::update(const SourcePose& sourceCurrent, const Mat4& componentToWorld, float deltaTime) {
    if (bound_) {
        retargeter_.apply(skeleton_, sourceCurrent, pose_);
        if (options_.enableSpringBones) springs_.update(pose_, componentToWorld, deltaTime);
    }
    evaluateExpressions(model_, expressionWeights_, morphWeights_);
    skinner_.skin(model_, pose_, morphWeights_, skinned_);
}

void Avatar::resetDynamics(const Mat4& componentToWorld) {
    if (bound_ && options_.enableSpringBones) springs_.reset(pose_, componentToWorld);
}

bool Avatar::setExpression(const std::string& presetOrName, float weight) {
    int i = findExpression(model_, presetOrName);
    if (i < 0) return false;
    expressionWeights_[i] = weight;
    return true;
}

}  // namespace rvrm
