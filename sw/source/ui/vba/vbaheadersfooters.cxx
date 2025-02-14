/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */
#include "vbaheadersfooters.hxx"
#include "vbaheaderfooter.hxx"
#include <cppuhelper/implbase.hxx>

using namespace ::ooo::vba;
using namespace ::com::sun::star;

namespace {

// I assume there is only one headersfooters in Writer
class HeadersFootersIndexAccess : public ::cppu::WeakImplHelper<container::XIndexAccess >
{
private:
    uno::Reference< XHelperInterface > mxParent;
    uno::Reference< uno::XComponentContext > mxContext;
    uno::Reference< frame::XModel > mxModel;
    uno::Reference< beans::XPropertySet > mxPageStyleProps;
    bool mbHeader;

public:
    HeadersFootersIndexAccess( const uno::Reference< XHelperInterface >& xParent, const uno::Reference< uno::XComponentContext >& xContext, const uno::Reference< frame::XModel >& xModel, const uno::Reference< beans::XPropertySet >& xPageStyleProps, bool bHeader ) : mxParent( xParent ), mxContext( xContext ), mxModel( xModel ), mxPageStyleProps( xPageStyleProps ), mbHeader( bHeader ) {}

    // XIndexAccess
    virtual sal_Int32 SAL_CALL getCount(  ) override
    {
        // first page, even pages and primary page
        return 3;
    }
    virtual uno::Any SAL_CALL getByIndex( sal_Int32 Index ) override
    {
        if( Index < 1 || Index > 3 )
            throw lang::IndexOutOfBoundsException();
        return uno::Any( uno::Reference< word::XHeaderFooter >( new SwVbaHeaderFooter( mxParent,  mxContext, mxModel, mxPageStyleProps, mbHeader, Index ) ) );
    }
    virtual uno::Type SAL_CALL getElementType(  ) override
    {
        return cppu::UnoType<word::XHeaderFooter>::get();
    }
    virtual sal_Bool SAL_CALL hasElements(  ) override
    {
        return true;
    }
};

class HeadersFootersEnumWrapper : public EnumerationHelper_BASE
{
    SwVbaHeadersFooters* pHeadersFooters;
    sal_Int32 nIndex;
public:
    explicit HeadersFootersEnumWrapper( SwVbaHeadersFooters* _pHeadersFooters ) : pHeadersFooters( _pHeadersFooters ), nIndex( 0 ) {}
    virtual sal_Bool SAL_CALL hasMoreElements(  ) override
    {
        return ( nIndex < pHeadersFooters->getCount() );
    }

    virtual uno::Any SAL_CALL nextElement(  ) override
    {
        if ( nIndex < pHeadersFooters->getCount() )
            return pHeadersFooters->Item( uno::Any( ++nIndex ), uno::Any() );
        throw container::NoSuchElementException();
    }
};

}

SwVbaHeadersFooters::SwVbaHeadersFooters( const uno::Reference< XHelperInterface >& xParent, const uno::Reference< uno::XComponentContext > & xContext, const uno::Reference< frame::XModel >& xModel, const uno::Reference< beans::XPropertySet >& xPageStyleProps, bool isHeader ): SwVbaHeadersFooters_BASE( xParent, xContext, new HeadersFootersIndexAccess( xParent, xContext, xModel, xPageStyleProps, isHeader ) ),  mxModel( xModel ), mxPageStyleProps( xPageStyleProps ), mbHeader( isHeader )
{
}

::sal_Int32 SAL_CALL SwVbaHeadersFooters::getCount()
{
    // wdHeaderFooterFirstPage, wdHeaderFooterPrimary and wdHeaderFooterEvenPages
    return 3;
}

uno::Any SAL_CALL SwVbaHeadersFooters::Item( const uno::Any& Index1, const uno::Any& )
{
    sal_Int32 nIndex = 0;
    Index1 >>= nIndex;
    if( ( nIndex < 1 ) || ( nIndex > 3 ) )
    {
        throw lang::IndexOutOfBoundsException();
    }
    return uno::Any( uno::Reference< word::XHeaderFooter >( new SwVbaHeaderFooter( this,  mxContext, mxModel, mxPageStyleProps, mbHeader, nIndex ) ) );
}

// XEnumerationAccess
uno::Type
SwVbaHeadersFooters::getElementType()
{
    return cppu::UnoType<word::XHeaderFooter>::get();
}
uno::Reference< container::XEnumeration >

SwVbaHeadersFooters::createEnumeration()
{
    return new HeadersFootersEnumWrapper( this );
}

uno::Any
SwVbaHeadersFooters::createCollectionObject( const uno::Any& aSource )
{
    return aSource;
}

OUString
SwVbaHeadersFooters::getServiceImplName()
{
    return "SwVbaHeadersFooters";
}

uno::Sequence<OUString>
SwVbaHeadersFooters::getServiceNames()
{
    static uno::Sequence< OUString > const sNames
    {
        "ooo.vba.word.HeadersFooters"
    };
    return sNames;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
