// Copyright 2007-2025 The SharpSvn Project
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "stdafx.h"

using namespace SharpSvn::Implementation;
using namespace SharpSvn;
using namespace System;
using namespace System::Collections::Generic;
using namespace System::Collections::ObjectModel;

namespace SharpSvn {
    ref class SvnConflictWalkBaton sealed
    {
    public:
        initonly SvnClient^ Client;
        initonly EventHandler<SvnConflictInfo^>^ Handler;

        SvnConflictWalkBaton(SvnClient^ client, EventHandler<SvnConflictInfo^>^ handler)
        {
            Client = client;
            Handler = handler;
        }
    };
}

static void validate_conflict_info(SvnClient^ client, SvnConflictInfo^ conflictInfo)
{
    if (!conflictInfo)
        throw gcnew ArgumentNullException("conflictInfo");
    else if (!Object::ReferenceEquals(conflictInfo->Owner, client))
        throw gcnew ArgumentException("Conflict info must be used with the SvnClient that created it.", "conflictInfo");

    conflictInfo->Ensure();
}

static void try_get_tree_conflict_description(
    SvnClient^ client,
    svn_client_conflict_t *conflict,
    AprPool^ pool,
    String^% incomingDescription,
    String^% localDescription)
{
    const char *incomingDescriptionPtr = nullptr;
    const char *localDescriptionPtr = nullptr;

    svn_error_t *error = svn_client_conflict_tree_get_description(
        &incomingDescriptionPtr,
        &localDescriptionPtr,
        conflict,
        client->CtxHandle,
        pool->Handle,
        pool->Handle);

    if (error)
    {
        svn_error_clear(error);
        return;
    }

    if (incomingDescriptionPtr)
        incomingDescription = SvnBase::Utf8_PtrToString(incomingDescriptionPtr);
    if (localDescriptionPtr)
        localDescription = SvnBase::Utf8_PtrToString(localDescriptionPtr);
}

static void read_incoming_conflict_location(
    SvnConflictInfo^ conflictInfo,
    AprPool^ pool,
    bool oldLocation)
{
    const char *relativePathPtr = nullptr;
    svn_revnum_t revision = SVN_INVALID_REVNUM;
    svn_node_kind_t nodeKind = svn_node_unknown;

    svn_error_t *error = oldLocation
        ? svn_client_conflict_get_incoming_old_repos_location(
            &relativePathPtr,
            &revision,
            &nodeKind,
            conflictInfo->Handle,
            pool->Handle,
            pool->Handle)
        : svn_client_conflict_get_incoming_new_repos_location(
            &relativePathPtr,
            &revision,
            &nodeKind,
            conflictInfo->Handle,
            pool->Handle,
            pool->Handle);

    if (error)
    {
        svn_error_clear(error);
        return;
    }

    String^ relativePath = relativePathPtr
        ? SvnBase::Utf8_PtrToString(relativePathPtr)
        : nullptr;

    if (oldLocation)
        conflictInfo->SetIncomingOldLocation(relativePath, revision, (SvnNodeKind)nodeKind);
    else
        conflictInfo->SetIncomingNewLocation(relativePath, revision, (SvnNodeKind)nodeKind);
}

static void refresh_conflict_repository_metadata(SvnConflictInfo^ conflictInfo, AprPool^ pool)
{
    const char *repositoryRootPtr = nullptr;
    const char *repositoryUuidPtr = nullptr;

    svn_error_t *error = svn_client_conflict_get_repos_info(
        &repositoryRootPtr,
        &repositoryUuidPtr,
        conflictInfo->Handle,
        pool->Handle,
        pool->Handle);

    if (!error)
    {
        conflictInfo->SetRepositoryMetadata(
            repositoryRootPtr ? SvnBase::Utf8_PtrToUri(repositoryRootPtr, SvnNodeKind::Directory) : nullptr,
            repositoryUuidPtr ? SvnBase::Utf8_PtrToString(repositoryUuidPtr) : nullptr);
    }
    else
    {
        svn_error_clear(error);
    }

    read_incoming_conflict_location(conflictInfo, pool, true);
    read_incoming_conflict_location(conflictInfo, pool, false);
}

static svn_error_t *svn_conflict_walk_receiver(void *baton, svn_client_conflict_t *conflict, apr_pool_t *scratchPool)
{
    UNUSED_ALWAYS(conflict);

    try
    {
        SvnConflictWalkBaton^ walkBaton = AprBaton<SvnConflictWalkBaton^>::Get((IntPtr)baton);
        const char *localPath = svn_client_conflict_get_local_abspath(conflict);
        if (!localPath)
            return nullptr;

        AprPool pool(scratchPool, false);
        String^ fullPath = SvnBase::Utf8_PathPtrToString(localPath, %pool);
        SvnConflictInfo^ info = walkBaton->Client->GetConflictInfo(fullPath);
        if (info)
            walkBaton->Handler(walkBaton->Client, info);

        return nullptr;
    }
    catch(Exception^ e)
    {
        return SvnException::CreateExceptionSvnError("Conflict walker", e);
    }
}

SvnConflictInfo::SvnConflictInfo(
    SvnClient^ owner,
    svn_client_conflict_t *conflict,
    AprPool^ pool,
    String^ fullPath,
    bool hasTreeConflict,
    bool hasTextConflict,
    int propConflictCount,
    String^ incomingChangeSummary,
    String^ localChangeSummary)
{
    if (!owner)
        throw gcnew ArgumentNullException("owner");
    else if (!conflict)
        throw gcnew ArgumentNullException("conflict");
    else if (!pool)
        throw gcnew ArgumentNullException("pool");

    _owner = owner;
    _conflict = conflict;
    _pool = pool;
    _fullPath = fullPath;
    _hasTreeConflict = hasTreeConflict;
    _hasTextConflict = hasTextConflict;
    _propConflictCount = propConflictCount;
    _incomingChangeSummary = incomingChangeSummary ? incomingChangeSummary : String::Empty;
    _localChangeSummary = localChangeSummary ? localChangeSummary : String::Empty;
    _operation = (SvnOperation)svn_client_conflict_get_operation(_conflict);
    _incomingChange = (SvnConflictAction)svn_client_conflict_get_incoming_change(_conflict);
    _localChange = (SvnConflictReason)svn_client_conflict_get_local_change(_conflict);
    _recommendedOptionId = (SvnConflictOptionId)svn_client_conflict_get_recommended_option_id(_conflict);
    _victimNodeKind = (SvnNodeKind)svn_client_conflict_tree_get_victim_node_kind(_conflict);
}

SvnConflictInfo::~SvnConflictInfo()
{
    this->!SvnConflictInfo();
}

SvnConflictInfo::!SvnConflictInfo()
{
    if (_pool)
    {
        delete _pool;
        _pool = nullptr;
    }

    _conflict = nullptr;
    _owner = nullptr;
}

void SvnConflictInfo::Ensure()
{
    if (!_conflict || !_pool)
        throw gcnew ObjectDisposedException("SvnConflictInfo");
}

void SvnConflictInfo::SetTreeDescriptions(String^ incomingChangeSummary, String^ localChangeSummary)
{
    _incomingChangeSummary = incomingChangeSummary ? incomingChangeSummary : String::Empty;
    _localChangeSummary = localChangeSummary ? localChangeSummary : String::Empty;
}

void SvnConflictInfo::SetRepositoryMetadata(Uri^ repositoryRoot, String^ repositoryUuid)
{
    _repositoryRoot = repositoryRoot;
    _repositoryUuid = repositoryUuid;
}

void SvnConflictInfo::SetIncomingOldLocation(String^ repositoryPath, __int64 revision, SvnNodeKind nodeKind)
{
    _incomingOldRepositoryPath = repositoryPath;
    _incomingOldRevision = revision;
    _incomingOldNodeKind = nodeKind;
}

void SvnConflictInfo::SetIncomingNewLocation(String^ repositoryPath, __int64 revision, SvnNodeKind nodeKind)
{
    _incomingNewRepositoryPath = repositoryPath;
    _incomingNewRevision = revision;
    _incomingNewNodeKind = nodeKind;
}

SvnConflictInfo^ SvnClient::GetConflictInfo(String^ path)
{
    if (String::IsNullOrEmpty(path))
        throw gcnew ArgumentNullException("path");
    else if (!IsNotUri(path))
        throw gcnew ArgumentException(SharpSvnStrings::ArgumentMustBeAPathNotAUri, "path");

    EnsureState(SvnContextState::AuthorizationInitialized);

    AprPool^ pool = gcnew AprPool(%_pool);
    NoArgsStore store(this, pool);

    svn_client_conflict_t *conflict = nullptr;
    svn_error_t *error = svn_client_conflict_get(
        &conflict,
        pool->AllocAbsoluteDirent(path),
        CtxHandle,
        pool->Handle,
        pool->Handle);

    if (error)
    {
        svn_error_clear(error);
        delete pool;
        return nullptr;
    }

    if (!conflict)
    {
        delete pool;
        return nullptr;
    }

    svn_boolean_t textConflicted = false;
    svn_boolean_t treeConflicted = false;
    apr_array_header_t *propConflicts = nullptr;

    error = svn_client_conflict_get_conflicted(
        &textConflicted,
        &propConflicts,
        &treeConflicted,
        conflict,
        pool->Handle,
        pool->Handle);

    if (error)
    {
        svn_error_clear(error);
        delete pool;
        return nullptr;
    }

    String^ incomingDescription = String::Empty;
    String^ localDescription = String::Empty;

    if (treeConflicted)
        try_get_tree_conflict_description(this, conflict, pool, incomingDescription, localDescription);

    String^ fullPath = SvnTools::GetNormalizedFullPath(path);
    const char *localPath = svn_client_conflict_get_local_abspath(conflict);
    if (localPath)
        fullPath = SvnBase::Utf8_PathPtrToString(localPath, pool);

    SvnConflictInfo^ info = gcnew SvnConflictInfo(
        this,
        conflict,
        pool,
        fullPath,
        treeConflicted != false,
        textConflicted != false,
        propConflicts ? propConflicts->nelts : 0,
        incomingDescription,
        localDescription);

    refresh_conflict_repository_metadata(info, pool);

    return info;
}

bool SvnClient::FetchTreeConflictDetails(SvnConflictInfo^ conflictInfo)
{
    validate_conflict_info(this, conflictInfo);

    EnsureState(SvnContextState::AuthorizationInitialized);
    AprPool scratchPool(%_pool);
    NoArgsStore store(this, %scratchPool);

    svn_error_t *error = svn_client_conflict_tree_get_details(
        conflictInfo->Handle,
        CtxHandle,
        scratchPool.Handle);

    if (error)
    {
        svn_error_clear(error);
        return false;
    }

    String^ incomingDescription = conflictInfo->IncomingChangeSummary;
    String^ localDescription = conflictInfo->LocalChangeSummary;
    try_get_tree_conflict_description(this, conflictInfo->Handle, conflictInfo->Pool, incomingDescription, localDescription);
    conflictInfo->SetTreeDescriptions(incomingDescription, localDescription);
    refresh_conflict_repository_metadata(conflictInfo, conflictInfo->Pool);

    return true;
}

ReadOnlyCollection<SvnConflictOption^>^ SvnClient::GetTreeResolutionOptions(SvnConflictInfo^ conflictInfo)
{
    validate_conflict_info(this, conflictInfo);

    EnsureState(SvnContextState::AuthorizationInitialized);
    AprPool scratchPool(%_pool);
    NoArgsStore store(this, %scratchPool);

    apr_array_header_t *options = nullptr;
    svn_error_t *error = svn_client_conflict_tree_get_resolution_options(
        &options,
        conflictInfo->Handle,
        CtxHandle,
        scratchPool.Handle,
        scratchPool.Handle);

    System::Collections::Generic::List<SharpSvn::SvnConflictOption^>^ result = gcnew System::Collections::Generic::List<SharpSvn::SvnConflictOption^>();

    if (error)
    {
        svn_error_clear(error);
        return gcnew ReadOnlyCollection<SvnConflictOption^>(result);
    }

    if (options)
    {
        for (int i = 0; i < options->nelts; i++)
        {
            svn_client_conflict_option_t *option = APR_ARRAY_IDX(options, i, svn_client_conflict_option_t *);
            if (!option)
                continue;

            result->Add(gcnew SvnConflictOption(
                (SvnConflictOptionId)svn_client_conflict_option_get_id(option),
                SvnBase::Utf8_PtrToString(svn_client_conflict_option_get_label(option, scratchPool.Handle)),
                SvnBase::Utf8_PtrToString(svn_client_conflict_option_get_description(option, scratchPool.Handle))));
        }
    }

    return gcnew ReadOnlyCollection<SvnConflictOption^>(result);
}

bool SvnClient::ResolveTreeConflict(SvnConflictInfo^ conflictInfo, SvnConflictOption^ option)
{
    if (!option)
        throw gcnew ArgumentNullException("option");

    return ResolveTreeConflictById(conflictInfo, option->Id);
}

bool SvnClient::ResolveTreeConflictById(SvnConflictInfo^ conflictInfo, SvnConflictOptionId optionId)
{
    validate_conflict_info(this, conflictInfo);

    EnsureState(SvnContextState::AuthorizationInitialized);
    AprPool scratchPool(%_pool);
    NoArgsStore store(this, %scratchPool);

    svn_error_t *error = svn_client_conflict_tree_resolve_by_id(
        conflictInfo->Handle,
        (svn_client_conflict_option_id_t)optionId,
        CtxHandle,
        scratchPool.Handle);

    if (error)
    {
        svn_error_clear(error);
        return false;
    }

    return true;
}

bool SvnClient::WalkConflicts(String^ path, EventHandler<SvnConflictInfo^>^ handler)
{
    return WalkConflicts(path, SvnDepth::Infinity, handler);
}

bool SvnClient::WalkConflicts(String^ path, SvnDepth depth, EventHandler<SvnConflictInfo^>^ handler)
{
    if (String::IsNullOrEmpty(path))
        throw gcnew ArgumentNullException("path");
    else if (!handler)
        throw gcnew ArgumentNullException("handler");
    else if (!IsNotUri(path))
        throw gcnew ArgumentException(SharpSvnStrings::ArgumentMustBeAPathNotAUri, "path");

    EnumVerifier::Verify(depth);

    EnsureState(SvnContextState::AuthorizationInitialized);
    AprPool pool(%_pool);
    NoArgsStore store(this, %pool);
    AprBaton<SvnConflictWalkBaton^> baton(gcnew SvnConflictWalkBaton(this, handler));

    svn_error_t *error = svn_client_conflict_walk(
        pool.AllocAbsoluteDirent(path),
        (svn_depth_t)depth,
        svn_conflict_walk_receiver,
        baton.Handle,
        CtxHandle,
        pool.Handle);

    if (error)
        throw SvnException::Create(error);

    return true;
}
