// Copyright (c) 2017-2018 Florian Wende (flwende@gmail.com)
//
// Distributed under the BSD 2-clause Software License
// (See accompanying file LICENSE)

#if !defined(PROXY_GEN_HPP)
#define PROXY_GEN_HPP

#include <iostream>
#include <string>
#include <vector>

#include <clang/AST/AST.h>
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

#include <misc/string_helper.hpp>
#include <misc/rewriter.hpp>
#include <misc/matcher.hpp>

#if !defined(TRAFO_NAMESPACE)
    #define TRAFO_NAMESPACE fw
#endif

namespace TRAFO_NAMESPACE
{
    using namespace TRAFO_NAMESPACE::internal;

    class ClassDefinition : public clang::ast_matchers::MatchFinder::MatchCallback
    {
    protected:

        Rewriter& rewriter;

        // meta data
        class MetaData
        {
            struct AccessSpecifier
            {
                const clang::AccessSpecDecl& decl;
                const clang::SourceLocation sourceLocation;
                const clang::SourceRange sourceRange;
                const clang::SourceLocation scopeBegin;

                AccessSpecifier(const clang::AccessSpecDecl& decl)
                    :
                    decl(decl),
                    sourceLocation(decl.getAccessSpecifierLoc()),
                    sourceRange(decl.getSourceRange()),
                    scopeBegin(decl.getColonLoc().getLocWithOffset(1))
                { ; }
            };

            struct Constructor
            {
                const clang::CXXConstructorDecl& decl;

                Constructor(const clang::CXXConstructorDecl& decl)
                    :
                    decl(decl)
                { ; }
            };

            struct MemberField
            {
                const clang::FieldDecl& decl;
                const std::string name;
                const std::string typeName;
                const bool isPublic;
                const bool isConst;
                const bool isFundamentalOrTemplateType;
                
                MemberField(const clang::FieldDecl& decl, const std::string typeName, const std::string name, const bool isPublic, const bool isConst, const bool isFundamentalOrTemplateType)
                    :
                    decl(decl),
                    name(name),
                    typeName(typeName),
                    isPublic(isPublic),
                    isConst(isConst),
                    isFundamentalOrTemplateType(isFundamentalOrTemplateType)
                { ; }
            };
            
            void collectMetaData()
            {
                Matcher matcher;

                // match all (public, private) fields of the class and collect information: 
                // declaration, const qualifier, fundamental type, type name, name
                std::string publicFieldTypeName;
                for (auto field : decl.fields())
                {
                    const clang::QualType qualType = field->getType();
                    const clang::Type* type = qualType.getTypePtrOrNull();

                    const std::string name = field->getType().getAsString();
                    const std::string typeName = field->getNameAsString();
                    const bool isPublic = Matcher::testDecl(*field, clang::ast_matchers::fieldDecl(clang::ast_matchers::isPublic()), context);
                    const bool isConstant = qualType.getQualifiers().hasConst();
                    const bool isFundamentalOrTemplateType = (type != nullptr ? (type->isFundamentalType() || type->isTemplateTypeParmType()) : false);
                    fields.emplace_back(*field, name, typeName, isPublic, isConstant, isFundamentalOrTemplateType);

                    if (isPublic)
                    {
                        const MemberField& thisField = fields.back();

                        if (numPublicFields == 0)
                        {
                            publicFieldTypeName = thisField.typeName;
                        }
                        else
                        {
                            hasMultiplePublicFieldTypes |= (thisField.typeName != publicFieldTypeName);
                        }

                        hasNonFundamentalPublicFields |= !(thisField.isFundamentalOrTemplateType);

                        ++numPublicFields;
                    }
                }
                
                // proxy class candidates must have at least 1 public field
                isProxyClassCandidate &= (numPublicFields > 0);
                if (!isProxyClassCandidate) return;

                // proxy class candidates must have fundamental public fields
                isProxyClassCandidate &= !hasNonFundamentalPublicFields;

                if (isProxyClassCandidate);
                {
                    using namespace clang::ast_matchers;

                    // match public and private access specifier
                    matcher.addMatcher(accessSpecDecl(hasParent(cxxRecordDecl(allOf(hasName(name), unless(isTemplateInstantiation()))))).bind("accessSpecDecl"), \
                        [&] (const MatchFinder::MatchResult& result) mutable \
                        {
                            if (const clang::AccessSpecDecl* decl = result.Nodes.getNodeAs<clang::AccessSpecDecl>("accessSpecDecl"))
                            {
                                publicAccess.emplace_back(*decl);
                            }
                        });
                    
                    // match all (public, private) constructors
                #define MATCH(MODIFIER, VARIABLE) \
                    matcher.addMatcher(cxxConstructorDecl(allOf(MODIFIER(), hasParent(cxxRecordDecl(allOf(hasName(name), unless(isTemplateInstantiation())))))).bind("constructorDecl"), \
                        [&] (const MatchFinder::MatchResult& result) mutable \
                        { \
                            if (const clang::CXXConstructorDecl* decl = result.Nodes.getNodeAs<clang::CXXConstructorDecl>("constructorDecl")) \
                            { \
                                VARIABLE.emplace_back(*decl); \
                            } \
                        })
                        
                    MATCH(isPublic, publicConstructors);
                    MATCH(isPrivate, privateConstructors);
                #undef MATCH

                    matcher.run(context);
                    matcher.clear();
                }
            }

        public:

            const clang::CXXRecordDecl& decl;
            clang::ASTContext& context;
            const std::string name;
            const bool isStruct;
            const bool isTemplated;
            const std::size_t numFields;
            std::size_t numPublicFields;
            bool hasNonFundamentalPublicFields;
            bool hasMultiplePublicFieldTypes;
            bool isProxyClassCandidate;
            std::vector<AccessSpecifier> publicAccess;
            std::vector<AccessSpecifier> privateAccess;
            std::vector<Constructor> publicConstructors;
            std::vector<Constructor> privateConstructors;
            std::vector<MemberField> fields;

            // proxy class candidates should not have any of these properties: abstract, polymorphic, empty AND
            // proxy class candidates must be class definitions and no specializations in case of template classes
            // note: a template class specialization is of type CXXRecordDecl, and we need to check for its describing template class
            //       (if it does not exist, then it is a normal C++ class)
            MetaData(const clang::CXXRecordDecl& decl, clang::ASTContext& context, const bool isTemplated)
                :
                decl(decl),
                context(context),
                name(decl.getNameAsString()),                
                isStruct(Matcher::testDecl(decl, clang::ast_matchers::recordDecl(clang::ast_matchers::isStruct()), context)),
                isTemplated(isTemplated),
                numFields(std::distance(decl.field_begin(), decl.field_end())),
                numPublicFields(0),
                hasNonFundamentalPublicFields(false),
                hasMultiplePublicFieldTypes(false),
                isProxyClassCandidate(numFields > 0 && (isTemplated ? true : decl.getDescribedClassTemplate() == nullptr) &&
                                      !(decl.isAbstract() || decl.isPolymorphic() || decl.isEmpty()))
            {
                if (isProxyClassCandidate)
                {
                    collectMetaData();
                }
            }

            clang::SourceLocation publicScopeStart()
            {
                if (publicAccess.size() > 0)
                {
                    return publicAccess[0].scopeBegin;
                }
                else if (isStruct)
                {
                    return decl.getBraceRange().getBegin().getLocWithOffset(1);
                }
                else
                {
                    return decl.getBraceRange().getEnd();
                }
            }
        };

        std::vector<MetaData> targetClasses;

        // implementation

        std::string createProxyClassStandardConstructor(MetaData& thisClass, const clang::CXXRecordDecl& decl) const
        {
            std::string constructor = thisClass.name + std::string("_proxy(");

            if (thisClass.hasMultiplePublicFieldTypes)
            {
                std::cerr << thisClass.name << ": createProxyClassStandardConstructor: not implemented yet" << std::endl;
            }
            else
            {
                std::string publicFieldType;
                for (auto field : thisClass.fields)
                {
                    if (field.isPublic)
                    {
                        publicFieldType = field.typeName;
                        break;
                    }
                }

                constructor += publicFieldType + std::string("* ptr, const std::size_t n");
                if (thisClass.numFields > thisClass.numPublicFields)
                {
                    for (auto field : thisClass.fields)
                    {
                        if (!field.isPublic)
                        {
                            constructor +=  std::string(", ") + field.typeName + std::string(" ") + field.name;
                        }
                    }
                }
                constructor += ")\n\t:\n\t";

                std::size_t publicFieldId = 0;
                std::size_t fieldId = 0;
                for (auto field : thisClass.fields)
                {
                    if (field.isPublic)
                    {
                        constructor += field.name + std::string("(ptr[") + std::to_string(publicFieldId) + std::string(" * n])");
                        ++publicFieldId;
                    }
                    else
                    {
                        constructor += field.name + std::string("(") + field.name + std::string(")");
                    }

                    constructor += std::string((fieldId + 1) == thisClass.numFields ? "\n\t{ ; }" : ",\n\t");
                    ++fieldId;
                }
            }

            return constructor;
        }

        // generate proxy class definition
        void generateProxyClass(MetaData& thisClass)
        {
            const clang::CXXRecordDecl& decl = thisClass.decl;
            clang::ASTContext& context = thisClass.context;

            // replace class name
            rewriter.replace(decl.getLocation(), thisClass.name + "_proxy");

            // remove original constructors
            for (auto ctor : decl.ctors())
            {
                rewriter.replace(ctor->getSourceRange(), "// constructor: removed");    
            }

            // insert standard constructor
            const std::string constructor = createProxyClassStandardConstructor(thisClass, decl) + "\n";
            if (thisClass.publicConstructors.size() > 0)
            {
                rewriter.replace(thisClass.publicConstructors[0].decl.getSourceRange(), constructor);
            }
            else
            {
                const clang::SourceLocation location = thisClass.publicScopeStart();
                rewriter.insert(location, "\n\t" + constructor, true, true);
            }

            // public variables: add reference qualifier
            addReferenceQualifierToPublicFields(thisClass, rewriter);
        }

        virtual void addReferenceQualifierToPublicFields(MetaData& , Rewriter& rewriter) = 0;

    public:
        
        ClassDefinition(Rewriter& rewriter)
            :
            rewriter(rewriter)
        { ; }
    };

    class CXXClassDefinition : public ClassDefinition
    {
        using Base = ClassDefinition;
        using Base::rewriter;
        using Base::targetClasses;

        virtual void addReferenceQualifierToPublicFields(MetaData& thisClass, Rewriter& rewriter)
        {
            using namespace clang::ast_matchers;
            
            rewriter.addMatcher(fieldDecl(allOf(isPublic(), hasParent(cxxRecordDecl(hasName(thisClass.name))))).bind("fieldDecl"),
                [] (const MatchFinder::MatchResult& result, Rewriter& rewriter)
                { 
                    if (const clang::FieldDecl* decl = result.Nodes.getNodeAs<clang::FieldDecl>("fieldDecl"))
                    {        
                        rewriter.replace(decl->getSourceRange(), decl->getType().getAsString() + "& " + decl->getNameAsString());
                    }
                });
            rewriter.run(thisClass.context);
            rewriter.clear();
        }

    public:
        
        CXXClassDefinition(Rewriter& rewriter)
            :
            Base(rewriter)
        { ; }

        virtual void run(const clang::ast_matchers::MatchFinder::MatchResult& result)
        {
            if (const clang::CXXRecordDecl* decl = result.Nodes.getNodeAs<clang::CXXRecordDecl>("classDecl"))
            {
                targetClasses.emplace_back(*decl, *result.Context, false);
                MetaData& thisClass = targetClasses.back();

                if (thisClass.isProxyClassCandidate)
                {      
                    const std::string original_class = dumpDeclToStringHumanReadable(decl, rewriter.getLangOpts(), false);
                    Base::generateProxyClass(thisClass);
                    rewriter.insert(decl->getSourceRange().getBegin(), original_class + ";\n\n", true, true);
                }
                else
                {
                    targetClasses.pop_back();
                }
            }
        }
    };

    class ClassTemplateDefinition : public ClassDefinition
    {
        using Base = ClassDefinition;
        using Base::rewriter;
        using Base::targetClasses;

        virtual void addReferenceQualifierToPublicFields(MetaData& thisClass, Rewriter& rewriter)
        {
            using namespace clang::ast_matchers;
            
            rewriter.addMatcher(fieldDecl(allOf(isPublic(), hasParent(cxxRecordDecl(allOf(hasName(thisClass.name), unless(isTemplateInstantiation())))))).bind("fieldDecl"),
                [] (const MatchFinder::MatchResult& result, Rewriter& rewriter)
                { 
                    if (const clang::FieldDecl* decl = result.Nodes.getNodeAs<clang::FieldDecl>("fieldDecl"))
                    {        
                        rewriter.replace(decl->getSourceRange(), decl->getType().getAsString() + "& " + decl->getNameAsString());
                    }
                });
            rewriter.run(thisClass.context);
            rewriter.clear();
        }

    public:
        
        ClassTemplateDefinition(Rewriter& rewriter)
            :
            Base(rewriter)
        { ; }
        
        virtual void run(const clang::ast_matchers::MatchFinder::MatchResult& result)
        {
            if (const clang::ClassTemplateDecl* decl = result.Nodes.getNodeAs<clang::ClassTemplateDecl>("classTemplateDecl"))
            {
                const bool isTemplatedDefinition = decl->isThisDeclarationADefinition();
                if (!isTemplatedDefinition) return;

                targetClasses.emplace_back(*(decl->getTemplatedDecl()), *result.Context, true);
                MetaData& thisClass = targetClasses.back();
                
                if (thisClass.isProxyClassCandidate)
                {
                    const std::string original_class = dumpDeclToStringHumanReadable(decl, rewriter.getLangOpts(), false);
                    Base::generateProxyClass(thisClass);
                    rewriter.insert(decl->getSourceRange().getBegin(), original_class + ";\n\n", true, true);
                }
                else
                {
                    targetClasses.pop_back();
                }
            }
        }
    };

    class FindProxyClassCandidate : public clang::ASTConsumer
    {
        Rewriter rewriter;
        CXXClassDefinition cxxClassHandler;
        ClassTemplateDefinition classTemplateHandler;
        clang::ast_matchers::MatchFinder matcher;
        
    public:
        
        FindProxyClassCandidate(clang::Rewriter& clangRewriter) 
            :
            rewriter(clangRewriter),
            cxxClassHandler(rewriter),
            classTemplateHandler(rewriter)
        {
            using namespace clang::ast_matchers;

            matcher.addMatcher(cxxRecordDecl(allOf(isDefinition(), unless(isTemplateInstantiation()))).bind("classDecl"), &cxxClassHandler);
            matcher.addMatcher(classTemplateDecl().bind("classTemplateDecl"), &classTemplateHandler);
        }

        void HandleTranslationUnit(clang::ASTContext& context) override
        {	
            matcher.matchAST(context);
        }
    };

    class InsertProxyClass : public clang::ASTFrontendAction
    {
        clang::Rewriter rewriter;
        
    public:
        
        InsertProxyClass() { ; }
        
        void EndSourceFileAction() override
        {
            rewriter.getEditBuffer(rewriter.getSourceMgr().getMainFileID()).write(llvm::outs());
        }
        
        std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(clang::CompilerInstance& ci, llvm::StringRef file) override
        {
            rewriter.setSourceMgr(ci.getSourceManager(), ci.getLangOpts());
            return llvm::make_unique<FindProxyClassCandidate>(rewriter);
        }
    };
}

#endif