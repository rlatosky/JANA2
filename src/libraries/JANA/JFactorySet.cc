
// Copyright 2020, Jefferson Science Associates, LLC.
// Subject to the terms in the LICENSE file found in the top-level directory.

#include <iterator>
#include <iostream>

#include "JFactorySet.h"
#include "JFactory.h"
#include "JMultifactory.h"
#include "JFactoryGenerator.h"

//---------------------------------
// JFactorySet    (Constructor)
//---------------------------------
JFactorySet::JFactorySet(void)
{

}

//---------------------------------
// JFactorySet    (Constructor)
//---------------------------------
JFactorySet::JFactorySet(const std::vector<JFactoryGenerator*>& generators)
{
    // Add all factories from all factory generators
    for(auto generator : generators){
        generator->GenerateFactories(this);
    }
}

//---------------------------------
// ~JFactorySet    (Destructor)
//---------------------------------
JFactorySet::~JFactorySet()
{
    /// The destructor will delete any factories in the set, unless mIsFactoryOwner is set to false.
    /// The only time mIsFactoryOwner should/can be set false is when a JMultifactory is using a JFactorySet internally
    /// to manage its JMultifactoryHelpers.
    if (mIsFactoryOwner) {
        for (auto& f : mFactories) delete f.second;
    }
    // Now that the factories are deleted, nothing can call the multifactories so it is safe to delete them as well
    for (auto* mf : mMultifactories) { delete mf; }
}

//---------------------------------
// Add
//---------------------------------
bool JFactorySet::Add(JFactory* aFactory)
{
    /// Add a JFactory to this JFactorySet. The JFactorySet assumes ownership of this factory.
    /// If the JFactorySet already contains a JFactory with the same key,
    /// throw an exception and let the user figure out what to do.
    /// This scenario occurs when the user has multiple JFactory<T> producing the
    /// same T JObject, and is not distinguishing between them via tags.

    auto typed_key = std::make_pair( aFactory->GetObjectType(), aFactory->GetTag() );
    auto untyped_key = std::make_pair( aFactory->GetObjectName(), aFactory->GetTag() );

    auto typed_result = mFactories.find(typed_key);
    auto untyped_result = mFactoriesFromString.find(untyped_key);

    if (typed_result != std::end(mFactories) || untyped_result != std::end(mFactoriesFromString)) {
        // Factory is duplicate. Since this almost certainly indicates a user error, and
        // the caller will not be able to do anything about it anyway, throw an exception.
        // We show the user which factory is causing this problem, including both plugin names
        std::string other_plugin_name;
        if (typed_result != std::end(mFactories)) {
            other_plugin_name = typed_result->second->GetPluginName();
        }
        else {
            other_plugin_name = untyped_result->second->GetPluginName();
        }
        auto ex = JException("Attempted to add duplicate factories");
        ex.function_name = "JFactorySet::Add";
        ex.instance_name = aFactory->GetPrefix();
        ex.type_name = aFactory->GetTypeName();
        ex.plugin_name = aFactory->GetPluginName() + ", " + other_plugin_name;
        throw ex;
    }

    mFactories[typed_key] = aFactory;
    mFactoriesFromString[untyped_key] = aFactory;
    return true;
}


bool JFactorySet::Add(JMultifactory *multifactory) {
    /// Add a JMultifactory to this JFactorySet. This JFactorySet takes ownership of its JMultifactoryHelpers,
    /// which was previously held by the JMultifactory.mHelpers JFactorySet.
    /// Ownership of the JMultifactory itself is shared among those helpers.

    auto helpers = multifactory->GetHelpers();
    for (auto fac : helpers->GetAllFactories()) {
        Add(fac);
    }
    helpers->mIsFactoryOwner = false;
    mMultifactories.push_back(multifactory);
    /// This is a little bit weird, but we are using a JFactorySet internally to JMultifactory in order to store and
    /// efficiently access its JMultifactoryHelpers. Ownership of the JMultifactoryHelpers is transferred to
    /// the enclosing JFactorySet.
    return true;
}

//---------------------------------
// GetFactory
//---------------------------------
JFactory* JFactorySet::GetFactory(const std::string& object_name, const std::string& tag) const
{
    auto untyped_key = std::make_pair(object_name, tag);
    auto it = mFactoriesFromString.find(untyped_key);
    if (it != std::end(mFactoriesFromString)) {
        if (it->second->GetLevel() != mLevel) {
            throw JException("Factory belongs to a different level on the event hierarchy!");
        }
        return it->second;
    }
    return nullptr;
}

//---------------------------------
// GetAllFactories
//---------------------------------
std::vector<JFactory*> JFactorySet::GetAllFactories() const {
    std::vector<JFactory*> results;
    for (auto p : mFactories) {
        results.push_back(p.second);
    }
    return results;
}

//---------------------------------
// GetAllMultifactories
//---------------------------------
std::vector<JMultifactory*> JFactorySet::GetAllMultifactories() const {
    std::vector<JMultifactory*> results;
    for (auto f : mMultifactories) {
        results.push_back(f);
    }
    return results;
}

//---------------------------------
// Print
//---------------------------------
void JFactorySet::Print() const
{
    size_t max_len = 0;
    for (auto p: mFactories) {
        if (p.second->GetLevel() != mLevel) continue;
        auto len = p.second->GetObjectName().length();
        if( len > max_len ) max_len = len;
    }

    max_len += 4;
    for (auto p: mFactories) {
        if (p.second->GetLevel() != mLevel) continue;
        auto name = p.second->GetObjectName();
        auto tag = p.second->GetTag();

        std::cout << std::string( max_len-name.length(), ' ') + name;
        if (!tag.empty()) std::cout << ":" << tag;
        std::cout << std::endl;
    }
}

//---------------------------------
// Clear
//---------------------------------
void JFactorySet::Clear() {

    for (const auto& sFactoryPair : mFactories) {
        auto sFactory = sFactoryPair.second;
        sFactory->ClearData();
        // This automatically clears multifactories because their data is stored in helper factories!
    }
}

//---------------------------------
// Finish
//---------------------------------
void JFactorySet::Finish() {
    for (auto& p : mFactories) {
        p.second->DoFinish();
    }
    for (auto& multifac : mMultifactories) {
        multifac->DoFinish();
    }
}

