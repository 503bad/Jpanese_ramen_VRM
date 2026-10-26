-- RamenVRM_Probe: Phase 2 の前に 1 回だけ使う調査用 MOD(読み取りのみ。ゲームの状態は変更しない)。
-- プレイヤーキャラが現れたら、構成・ボーン・必要な関数の有無をログに書き、オブジェクト一覧と C++ ヘッダーを出力する。
-- 結果: ue4ss/UE4SS.log(行頭 [RamenVRM-Probe])、ue4ss/UE4SS_ObjectDump.txt、ue4ss/CXXHeaderDump/

local TAG = "[RamenVRM-Probe] "
local function log(s) print(TAG .. tostring(s) .. "\n") end
local function try(f, ...)
    local ok, r = pcall(f, ...)
    if ok then return r end
    return nil
end
local function fullName(o)
    if o == nil then return "nil" end
    local n = try(function() return o:GetFullName() end)
    return n or tostring(o)
end
local function valid(o)
    return o ~= nil and (try(function() return o:IsValid() end) == true)
end
local function vecStr(v)
    if v == nil then return "nil" end
    return string.format("(%.3f, %.3f, %.3f)", try(function() return v.X end) or 0/0,
        try(function() return v.Y end) or 0/0, try(function() return v.Z end) or 0/0)
end
local function quatStr(q)
    if q == nil then return "nil" end
    return string.format("(%.5f, %.5f, %.5f, %.5f)", try(function() return q.X end) or 0/0,
        try(function() return q.Y end) or 0/0, try(function() return q.Z end) or 0/0, try(function() return q.W end) or 0/0)
end
local function xfStr(t)
    if t == nil then return "nil" end
    return "T=" .. vecStr(try(function() return t.Translation end)) ..
        " R=" .. quatStr(try(function() return t.Rotation end)) ..
        " S=" .. vecStr(try(function() return t.Scale3D end))
end

log("loaded (UE4SS is running)")

-- ---- MOD 本体で使う予定の関数が存在するか
local function checkFunctions()
    local names = {
        "/Script/Engine.SkinnedMeshComponent:GetNumBones",
        "/Script/Engine.SkinnedMeshComponent:GetBoneName",
        "/Script/Engine.SkinnedMeshComponent:GetBoneIndex",
        "/Script/Engine.SkinnedMeshComponent:GetParentBone",
        "/Script/Engine.SkinnedMeshComponent:GetRefPoseTransform",
        "/Script/Engine.SkinnedMeshComponent:GetRefPosePosition",
        "/Script/Engine.SkinnedMeshComponent:GetBoneTransform",
        "/Script/Engine.SkinnedMeshComponent:SetLeaderPoseComponent",
        "/Script/Engine.SkinnedMeshComponent:SetSkinnedAssetAndUpdate",
        "/Script/Engine.SkeletalMeshComponent:SetSkeletalMeshAsset",
        "/Script/Engine.SkeletalMeshComponent:GetAnimInstance",
        "/Script/Engine.SceneComponent:GetSocketTransform",
        "/Script/Engine.SceneComponent:K2_GetComponentToWorld",
        "/Script/Engine.SceneComponent:SetVisibility",
        "/Script/Engine.SceneComponent:SetHiddenInGame",
        "/Script/Engine.SceneComponent:K2_AttachToComponent",
        "/Script/Engine.PrimitiveComponent:SetMaterial",
        "/Script/Engine.PrimitiveComponent:SetOwnerNoSee",
        "/Script/Engine.PrimitiveComponent:SetOnlyOwnerSee",
        "/Script/Engine.Actor:AddComponentByClass",
        "/Script/Engine.Actor:K2_DestroyComponent",
        "/Script/ProceduralMeshComponent.ProceduralMeshComponent:CreateMeshSection_LinearColor",
        "/Script/ProceduralMeshComponent.ProceduralMeshComponent:UpdateMeshSection_LinearColor",
        "/Script/ProceduralMeshComponent.ProceduralMeshComponent:ClearAllMeshSections",
        "/Script/Engine.KismetRenderingLibrary:ImportBufferAsTexture2D",
        "/Script/Engine.KismetRenderingLibrary:ImportFileAsTexture2D",
        "/Script/Engine.KismetMaterialLibrary:CreateDynamicMaterialInstance",
        "/Script/Engine.MaterialInstanceDynamic:SetTextureParameterValue",
        "/Script/Engine.MaterialInstanceDynamic:SetVectorParameterValue",
        "/Script/Engine.MaterialInstanceDynamic:SetScalarParameterValue",
        "/Script/GeometryFramework.DynamicMeshComponent:SetDynamicMesh",
    }
    for _, n in ipairs(names) do
        local f = try(function() return StaticFindObject(n) end)
        log("func " .. (valid(f) and "OK      " or "MISSING ") .. n)
    end
    for _, n in ipairs({ "/Script/ProceduralMeshComponent.ProceduralMeshComponent",
                         "/Script/GeometryFramework.DynamicMeshComponent",
                         "/Script/Engine.PoseableMeshComponent" }) do
        log("class " .. (valid(try(function() return StaticFindObject(n) end)) and "OK      " or "MISSING ") .. n)
    end
end

-- ---- クラスのプロパティ(オフセット付き)
local function dumpClassProps(obj, maxDepth)
    local cls = try(function() return obj:GetClass() end)
    local depth = 0
    while valid(cls) and depth < (maxDepth or 3) do
        log("  class " .. fullName(cls))
        try(function()
            cls:ForEachProperty(function(p)
                log(string.format("    0x%04X %s %s", p:GetOffset_Internal(), p:GetClass():GetFName():ToString(),
                    p:GetFName():ToString()))
            end)
        end)
        cls = try(function() return cls:GetSuperStruct() end)
        depth = depth + 1
    end
end

-- ---- スケルタルメッシュの詳細
local function dumpSkeletal(c)
    log("  SkeletalMeshComponent " .. fullName(c))
    log("    asset        : " .. fullName(try(function() return c.SkeletalMeshAsset end) or try(function() return c.SkeletalMesh end)))
    log("    animInstance : " .. fullName(try(function() return c:GetAnimInstance() end)))
    log("    leaderPose   : " .. fullName(try(function() return c.LeaderPoseComponent end)))
    log("    visible      : " .. tostring(try(function() return c:IsVisible() end)))
    log("    attachParent : " .. fullName(try(function() return c:GetAttachParent() end)))
    log("    relLoc       : " .. vecStr(try(function() return c.RelativeLocation end)))
    log("    relRot(PYR)  : " .. tostring(try(function() local r = c.RelativeRotation; return string.format("%.2f %.2f %.2f", r.Pitch, r.Yaw, r.Roll) end)))
    log("    compToWorld  : " .. xfStr(try(function() return c:K2_GetComponentToWorld() end)))
    local n = try(function() return c:GetNumBones() end) or 0
    log("    numBones     : " .. tostring(n))
    for i = 0, n - 1 do
        local name = try(function() return c:GetBoneName(i):ToString() end) or "?"
        local parent = try(function() return c:GetParentBone(FName(name)):ToString() end) or "?"
        local ref = try(function() return c:GetRefPoseTransform(i) end)
        local cur = try(function() return c:GetSocketTransform(FName(name), 2) end)  -- 2 = RTS_Component
        log(string.format("    bone %3d %-28s parent=%-24s ref[%s] cur[%s]", i, name, parent, xfStr(ref), xfStr(cur)))
    end
end

local function dumpActor(a, label)
    log("== " .. label .. ": " .. fullName(a))
    local cls = try(function() return a:GetClass() end)
    local chain = {}
    while valid(cls) do
        table.insert(chain, fullName(cls))
        cls = try(function() return cls:GetSuperStruct() end)
    end
    log("  class chain: " .. table.concat(chain, " <- "))
    log("  isLocallyControlled: " .. tostring(try(function() return a:IsLocallyControlled() end)))
    -- 持ち主が a のコンポーネントを列挙
    local comps = FindAllOf("ActorComponent") or {}
    local aName = fullName(a)
    for _, c in ipairs(comps) do
        local owner = try(function() return c:GetOwner() end)
        if valid(owner) and fullName(owner) == aName then
            log("  component " .. fullName(c))
            local isSk = try(function() return c:IsA(StaticFindObject("/Script/Engine.SkeletalMeshComponent")) end)
            if isSk then dumpSkeletal(c) end
        end
    end
end

local function findPlayerPawn()
    local pcs = FindAllOf("PlayerController") or {}
    for _, pc in ipairs(pcs) do
        if valid(pc) and try(function() return pc:IsLocalPlayerController() end) then
            local p = try(function() return pc.Pawn end)
            if valid(p) then return p, pc end
        end
    end
    return nil, nil
end

local done = false
local attempts = 0
log("waiting for the player character...")
LoopAsync(3000, function()
    if done then return true end
    attempts = attempts + 1
    ExecuteInGameThread(function()
        if done then return end
        local pawn, pc = findPlayerPawn()
        if not pawn then
            if attempts % 10 == 0 then log("still waiting (" .. attempts .. ")") end
            return
        end
        done = true
        log("player found after " .. attempts .. " checks")
        try(checkFunctions)
        log("== PlayerController: " .. fullName(pc))
        try(dumpActor, pawn, "local player pawn")
        for _, cn in ipairs({ "BP_Men_PlayerCharacter_C", "BP_Men_ReplicaPlayerCharacter_C" }) do
            local all = FindAllOf(cn) or {}
            log("instances of " .. cn .. ": " .. #all)
            for i, a in ipairs(all) do
                if fullName(a) ~= fullName(pawn) then try(dumpActor, a, cn .. " #" .. i) end
            end
        end
        log("== pawn class properties")
        try(dumpClassProps, pawn, 4)
        log("dumping objects (UE4SS_ObjectDump.txt)...")
        try(DumpAllObjects)
        log("generating C++ headers (CXXHeaderDump)... this can take a while")
        try(GenerateSDK)
        log("ALL DONE - you can quit the game now")
    end)
    return done
end)
