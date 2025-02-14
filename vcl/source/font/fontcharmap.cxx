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
#include <utility>
#include <vcl/fontcharmap.hxx>
#include <impfontcharmap.hxx>
#include <rtl/textcvt.h>
#include <rtl/textenc.h>
#include <sal/log.hxx>

#include <algorithm>
#include <vector>
#include <o3tl/sorted_vector.hxx>

CmapResult::CmapResult( bool bSymbolic,
    const sal_UCS4* pRangeCodes, int nRangeCount )
:   mpRangeCodes( pRangeCodes)
,   mpStartGlyphs( nullptr)
,   mpGlyphIds( nullptr)
,   mnRangeCount( nRangeCount)
,   mbSymbolic( bSymbolic)
,   mbRecoded( false)
{}

static ImplFontCharMapRef g_pDefaultImplFontCharMap;
const sal_UCS4 aDefaultUnicodeRanges[] = {0x0020,0xD800, 0xE000,0xFFF0};
const sal_UCS4 aDefaultSymbolRanges[] = {0x0020,0x0100, 0xF020,0xF100};

ImplFontCharMap::~ImplFontCharMap()
{
    if( !isDefaultMap() )
    {
        delete[] mpRangeCodes;
        delete[] mpStartGlyphs;
        delete[] mpGlyphIds;
    }
}

ImplFontCharMap::ImplFontCharMap( const CmapResult& rCR )
:   mpRangeCodes( rCR.mpRangeCodes )
,   mpStartGlyphs( rCR.mpStartGlyphs )
,   mpGlyphIds( rCR.mpGlyphIds )
,   mnRangeCount( rCR.mnRangeCount )
,   mnCharCount( 0 )
    , m_bSymbolic(rCR.mbSymbolic)
{
    const sal_UCS4* pRangePtr = mpRangeCodes;
    for( int i = mnRangeCount; --i >= 0; pRangePtr += 2 )
    {
        sal_UCS4 cFirst = pRangePtr[0];
        sal_UCS4 cLast  = pRangePtr[1];
        mnCharCount += cLast - cFirst;
    }
}

ImplFontCharMapRef const & ImplFontCharMap::getDefaultMap( bool bSymbols )
{
    const sal_UCS4* pRangeCodes = aDefaultUnicodeRanges;
    int nCodesCount = std::size(aDefaultUnicodeRanges);
    if( bSymbols )
    {
        pRangeCodes = aDefaultSymbolRanges;
        nCodesCount = std::size(aDefaultSymbolRanges);
    }

    CmapResult aDefaultCR( bSymbols, pRangeCodes, nCodesCount/2 );
    g_pDefaultImplFontCharMap = ImplFontCharMapRef(new ImplFontCharMap(aDefaultCR));

    return g_pDefaultImplFontCharMap;
}

bool ImplFontCharMap::isDefaultMap() const
{
    const bool bIsDefault = (mpRangeCodes == aDefaultUnicodeRanges) || (mpRangeCodes == aDefaultSymbolRanges);
    return bIsDefault;
}

static unsigned GetUInt( const unsigned char* p ) { return((p[0]<<24)+(p[1]<<16)+(p[2]<<8)+p[3]);}
static unsigned GetUShort( const unsigned char* p ){ return((p[0]<<8) | p[1]);}
static int GetSShort( const unsigned char* p ){ return static_cast<sal_Int16>((p[0]<<8)|p[1]);}

// TODO: move CMAP parsing directly into the ImplFontCharMap class
bool ParseCMAP( const unsigned char* pCmap, int nLength, CmapResult& rResult )
{
    rResult.mpRangeCodes = nullptr;
    rResult.mpStartGlyphs= nullptr;
    rResult.mpGlyphIds   = nullptr;
    rResult.mnRangeCount = 0;
    rResult.mbRecoded    = false;
    rResult.mbSymbolic   = false;

    // parse the table header and check for validity
    if( !pCmap || (nLength < 24) )
        return false;

    if( GetUShort( pCmap ) != 0x0000 ) // simple check for CMAP corruption
        return false;

    int nSubTables = GetUShort( pCmap + 2 );
    if( (nSubTables <= 0) || (nSubTables > (nLength - 24) / 8) )
        return false;

    const unsigned char* pEndValidArea = pCmap + nLength;

    // find the most interesting subtable in the CMAP
    rtl_TextEncoding eRecodeFrom = RTL_TEXTENCODING_UNICODE;
    int nOffset = 0;
    int nFormat = -1;
    int nBestVal = 0;
    for( const unsigned char* p = pCmap + 4; --nSubTables >= 0; p += 8 )
    {
        int nPlatform = GetUShort( p );
        int nEncoding = GetUShort( p+2 );
        int nPlatformEncoding = (nPlatform << 8) + nEncoding;

        int nValue;
        rtl_TextEncoding eTmpEncoding = RTL_TEXTENCODING_UNICODE;
        switch( nPlatformEncoding )
        {
            case 0x000: nValue = 20; break;                             // Unicode 1.0
            case 0x001: nValue = 21; break;                             // Unicode 1.1
            case 0x002: nValue = 22; break;                             // iso10646_1993
            case 0x003: nValue = 23; break;                             // UCS-2
            case 0x004: nValue = 24; break;                             // UCS-4
            case 0x100: nValue = 22; break;                             // Mac Unicode<2.0
            case 0x103: nValue = 23; break;                             // Mac Unicode>2.0
            case 0x300: nValue =  5; rResult.mbSymbolic = true; break;  // Win Symbol
            case 0x301: nValue = 28; break;                             // Win UCS-2
            case 0x30A: nValue = 29; break;                             // Win-UCS-4
            case 0x302: nValue = 11; eTmpEncoding = RTL_TEXTENCODING_SHIFT_JIS; break;
            case 0x303: nValue = 12; eTmpEncoding = RTL_TEXTENCODING_GB_18030; break;
            case 0x304: nValue = 11; eTmpEncoding = RTL_TEXTENCODING_BIG5; break;
            case 0x305: nValue = 11; eTmpEncoding = RTL_TEXTENCODING_MS_949; break;
            case 0x306: nValue = 11; eTmpEncoding = RTL_TEXTENCODING_MS_1361; break;
            default:    nValue = 0; break;
        }

        if( nValue <= 0 )   // ignore unknown encodings
            continue;

        int nTmpOffset = GetUInt( p+4 );

        if (nTmpOffset > nLength - 2 || nTmpOffset < 0)
            continue;

        int nTmpFormat = GetUShort( pCmap + nTmpOffset );
        if( nTmpFormat == 12 )                  // 32bit code -> glyph map format
            nValue += 3;
        else if( nTmpFormat != 4 )              // 16bit code -> glyph map format
            continue;                           // ignore other formats

        if( nBestVal < nValue )
        {
            nBestVal = nValue;
            nOffset = nTmpOffset;
            nFormat = nTmpFormat;
            eRecodeFrom = eTmpEncoding;
        }
    }

    // parse the best CMAP subtable
    int nRangeCount = 0;
    sal_UCS4* pCodePairs = nullptr;
    int* pStartGlyphs = nullptr;

    std::vector<sal_uInt16> aGlyphIdArray;
    aGlyphIdArray.reserve( 0x1000 );
    aGlyphIdArray.push_back( 0 );

    // format 4, the most common 16bit char mapping table
    if( (nFormat == 4) && ((nOffset+16) < nLength) )
    {
        int nSegCountX2 = GetUShort( pCmap + nOffset + 6 );
        nRangeCount = nSegCountX2/2 - 1;
        if (nRangeCount < 0)
        {
            SAL_WARN("vcl.gdi", "negative RangeCount");
            nRangeCount = 0;
        }

        const unsigned char* pLimitBase = pCmap + nOffset + 14;
        const unsigned char* pBeginBase = pLimitBase + nSegCountX2 + 2;
        const unsigned char* pDeltaBase = pBeginBase + nSegCountX2;
        const unsigned char* pOffsetBase = pDeltaBase + nSegCountX2;

        const int nOffsetBaseStart = pOffsetBase - pCmap;
        const int nRemainingLen = nLength - nOffsetBaseStart;
        const int nMaxPossibleRangeOffsets = nRemainingLen / 2;
        if (nRangeCount > nMaxPossibleRangeOffsets)
        {
            SAL_WARN("vcl.gdi", "more range offsets requested then space available");
            nRangeCount = std::max(0, nMaxPossibleRangeOffsets);
        }

        pCodePairs = new sal_UCS4[ nRangeCount * 2 ];
        pStartGlyphs = new int[ nRangeCount ];

        sal_UCS4* pCP = pCodePairs;
        for( int i = 0; i < nRangeCount; ++i )
        {
            const sal_UCS4 cMinChar = GetUShort( pBeginBase + 2*i );
            const sal_UCS4 cMaxChar = GetUShort( pLimitBase + 2*i );
            const int nGlyphDelta  = GetSShort( pDeltaBase + 2*i );
            const int nRangeOffset = GetUShort( pOffsetBase + 2*i );
            if( cMinChar > cMaxChar ) {  // no sane font should trigger this
                SAL_WARN("vcl.gdi", "Min char should never be more than the max char!");
                break;
            }
            if( cMaxChar == 0xFFFF ) {
                SAL_WARN("vcl.gdi", "Format 4 char should not be 0xFFFF");
                break;
            }
            if( !nRangeOffset ) {
                // glyphid can be calculated directly
                pStartGlyphs[i] = (cMinChar + nGlyphDelta) & 0xFFFF;
            } else {
                // update the glyphid-array with the glyphs in this range
                pStartGlyphs[i] = -static_cast<int>(aGlyphIdArray.size());
                const unsigned char* pGlyphIdPtr = pOffsetBase + 2*i + nRangeOffset;
                const size_t nRemainingSize = pEndValidArea >= pGlyphIdPtr ? pEndValidArea - pGlyphIdPtr : 0;
                const size_t nMaxPossibleRecords = nRemainingSize/2;
                if (nMaxPossibleRecords == 0) {  // no sane font should trigger this
                    SAL_WARN("vcl.gdi", "More indexes claimed that space available in font!");
                    break;
                }
                const size_t nMaxLegalChar = cMinChar + nMaxPossibleRecords-1;
                if (cMaxChar > nMaxLegalChar) {  // no sane font should trigger this
                    SAL_WARN("vcl.gdi", "More indexes claimed that space available in font!");
                    break;
                }
                for( sal_UCS4 c = cMinChar; c <= cMaxChar; ++c, pGlyphIdPtr+=2 ) {
                    const int nGlyphIndex = GetUShort( pGlyphIdPtr ) + nGlyphDelta;
                    aGlyphIdArray.push_back( static_cast<sal_uInt16>(nGlyphIndex) );
                }
            }
            *(pCP++) = cMinChar;
            *(pCP++) = cMaxChar + 1;
        }
        nRangeCount = (pCP - pCodePairs) / 2;
    }
    // format 12, the most common 32bit char mapping table
    else if( (nFormat == 12) && ((nOffset+16) < nLength) )
    {
        nRangeCount = GetUInt( pCmap + nOffset + 12 );
        if (nRangeCount < 0)
        {
            SAL_WARN("vcl.gdi", "negative RangeCount");
            nRangeCount = 0;
        }

        const int nGroupOffset = nOffset + 16;
        const int nRemainingLen = nLength - nGroupOffset;
        const int nMaxPossiblePairs = nRemainingLen / 12;
        if (nRangeCount > nMaxPossiblePairs)
        {
            SAL_WARN("vcl.gdi", "more code pairs requested then space available");
            nRangeCount = std::max(0, nMaxPossiblePairs);
        }

        pCodePairs = new sal_UCS4[ nRangeCount * 2 ];
        pStartGlyphs = new int[ nRangeCount ];

        const unsigned char* pGroup = pCmap + nGroupOffset;
        sal_UCS4* pCP = pCodePairs;
        for( int i = 0; i < nRangeCount; ++i )
        {
            sal_UCS4 cMinChar = GetUInt( pGroup + 0 );
            sal_UCS4 cMaxChar = GetUInt( pGroup + 4 );
            int nGlyphId = GetUInt( pGroup + 8 );
            pGroup += 12;

            if( cMinChar > cMaxChar ) {   // no sane font should trigger this
                SAL_WARN("vcl.gdi", "Min char should never be more than the max char!");
                break;
            }

            *(pCP++) = cMinChar;
            *(pCP++) = cMaxChar + 1;
            pStartGlyphs[i] = nGlyphId;
        }
        nRangeCount = (pCP - pCodePairs) / 2;
    }

    // check if any subtable resulted in something usable
    if( nRangeCount <= 0 )
    {
        delete[] pCodePairs;
        delete[] pStartGlyphs;

        // even when no CMAP is available we know it for symbol fonts
        if( rResult.mbSymbolic )
        {
            pCodePairs = new sal_UCS4[4];
            pCodePairs[0] = 0x0020;    // aliased symbols
            pCodePairs[1] = 0x0100;
            pCodePairs[2] = 0xF020;    // original symbols
            pCodePairs[3] = 0xF100;
            rResult.mpRangeCodes = pCodePairs;
            rResult.mnRangeCount = 2;
            return true;
        }

        return false;
    }

    // recode the code ranges to their unicode encoded ranges if needed
    rtl_TextToUnicodeConverter aConverter = nullptr;
    rtl_UnicodeToTextContext aCvtContext = nullptr;

    rResult.mbRecoded = ( eRecodeFrom != RTL_TEXTENCODING_UNICODE );
    if( rResult.mbRecoded )
    {
        aConverter = rtl_createTextToUnicodeConverter( eRecodeFrom );
        aCvtContext = rtl_createTextToUnicodeContext( aConverter );
    }

    if( aConverter && aCvtContext )
    {
        // determine the set of supported code points from encoded ranges
        o3tl::sorted_vector<sal_UCS4> aSupportedCodePoints;
        aSupportedCodePoints.reserve(256);

        static const int NINSIZE = 64;
        static const int NOUTSIZE = 64;
        std::vector<char> cCharsInp;
        cCharsInp.reserve(NINSIZE);
        sal_Unicode cCharsOut[ NOUTSIZE ];
        sal_UCS4* pCP = pCodePairs;
        for( int i = 0; i < nRangeCount; ++i )
        {
            sal_UCS4 cMin = *(pCP++);
            sal_UCS4 cEnd = *(pCP++);
            // ofz#25868 the conversion only makes sense with
            // input codepoints in 0..SAL_MAX_UINT16 range
            while (cMin < cEnd && cMin <= SAL_MAX_UINT16)
            {
                for (int j = 0; (cMin < cEnd) && (j < NINSIZE); ++cMin, ++j)
                {
                    if( cMin >= 0x0100 )
                        cCharsInp.push_back(static_cast<char>(cMin >> 8));
                    if( (cMin >= 0x0100) || (cMin < 0x00A0)  )
                        cCharsInp.push_back(static_cast<char>(cMin));
                }

                sal_uInt32 nCvtInfo;
                sal_Size nSrcCvtBytes;
                int nOutLen = rtl_convertTextToUnicode(
                    aConverter, aCvtContext,
                    cCharsInp.data(), cCharsInp.size(), cCharsOut, NOUTSIZE,
                    RTL_TEXTTOUNICODE_FLAGS_INVALID_IGNORE
                    | RTL_TEXTTOUNICODE_FLAGS_UNDEFINED_IGNORE,
                    &nCvtInfo, &nSrcCvtBytes );

                cCharsInp.clear();

                for (int j = 0; j < nOutLen; ++j)
                    aSupportedCodePoints.insert( cCharsOut[j] );
            }
        }

        rtl_destroyTextToUnicodeConverter( aCvtContext );
        rtl_destroyTextToUnicodeConverter( aConverter );

        // convert the set of supported code points to ranges
        std::vector<sal_UCS4> aSupportedRanges;

        for (auto const& supportedPoint : aSupportedCodePoints)
        {
            if( aSupportedRanges.empty()
            || (aSupportedRanges.back() != supportedPoint) )
            {
                // add new range beginning with current unicode
                aSupportedRanges.push_back(supportedPoint);
                aSupportedRanges.push_back( 0 );
            }

            // extend existing range to include current unicode
            aSupportedRanges.back() = supportedPoint + 1;
        }

        // glyph mapping for non-unicode fonts not implemented
        delete[] pStartGlyphs;
        pStartGlyphs = nullptr;
        aGlyphIdArray.clear();

        // make a pCodePairs array using the vector from above
        delete[] pCodePairs;
        nRangeCount = aSupportedRanges.size() / 2;
        if( nRangeCount <= 0 )
            return false;
        pCodePairs = new sal_UCS4[ nRangeCount * 2 ];
        pCP = pCodePairs;
        for (auto const& supportedRange : aSupportedRanges)
            *(pCP++) = supportedRange;
    }

    // prepare the glyphid-array if needed
    // TODO: merge ranges if they are close enough?
    sal_uInt16* pGlyphIds = nullptr;
    if( !aGlyphIdArray.empty())
    {
        pGlyphIds = new sal_uInt16[ aGlyphIdArray.size() ];
        sal_uInt16* pOut = pGlyphIds;
        for (auto const& glyphId : aGlyphIdArray)
            *(pOut++) = glyphId;
    }

    // update the result struct
    rResult.mpRangeCodes = pCodePairs;
    rResult.mpStartGlyphs = pStartGlyphs;
    rResult.mnRangeCount = nRangeCount;
    rResult.mpGlyphIds = pGlyphIds;
    return true;
}

FontCharMap::FontCharMap()
    : mpImplFontCharMap( ImplFontCharMap::getDefaultMap() )
{
}

FontCharMap::FontCharMap( ImplFontCharMapRef pIFCMap )
    : mpImplFontCharMap(std::move( pIFCMap ))
{
}

FontCharMap::FontCharMap( const CmapResult& rCR )
    : mpImplFontCharMap(new ImplFontCharMap(rCR))
{
}

FontCharMap::~FontCharMap()
{
    mpImplFontCharMap = nullptr;
}

FontCharMapRef FontCharMap::GetDefaultMap( bool bSymbol )
{
    FontCharMapRef xFontCharMap( new FontCharMap( ImplFontCharMap::getDefaultMap( bSymbol ) ) );
    return xFontCharMap;
}

bool FontCharMap::IsDefaultMap() const
{
    return mpImplFontCharMap->isDefaultMap();
}

bool FontCharMap::isSymbolic() const { return mpImplFontCharMap->m_bSymbolic; }

int FontCharMap::GetCharCount() const
{
    return mpImplFontCharMap->mnCharCount;
}

int FontCharMap::CountCharsInRange( sal_UCS4 cMin, sal_UCS4 cMax ) const
{
    int nCount = 0;

    // find and adjust range and char count for cMin
    int nRangeMin = findRangeIndex( cMin );
    if( nRangeMin & 1 )
        ++nRangeMin;
    else if( cMin > mpImplFontCharMap->mpRangeCodes[ nRangeMin ] )
        nCount -= cMin - mpImplFontCharMap->mpRangeCodes[ nRangeMin ];

    // find and adjust range and char count for cMax
    int nRangeMax = findRangeIndex( cMax );
    if( nRangeMax & 1 )
        --nRangeMax;
    else
        nCount -= mpImplFontCharMap->mpRangeCodes[ nRangeMax+1 ] - cMax - 1;

    // count chars in complete ranges between cMin and cMax
    for( int i = nRangeMin; i <= nRangeMax; i+=2 )
        nCount += mpImplFontCharMap->mpRangeCodes[i+1] - mpImplFontCharMap->mpRangeCodes[i];

    return nCount;
}

bool FontCharMap::HasChar( sal_UCS4 cChar ) const
{
    bool bHasChar = false;

    if( mpImplFontCharMap->mpStartGlyphs  == nullptr ) { // only the char-ranges are known
        const int nRange = findRangeIndex( cChar );
        if( nRange==0 && cChar < mpImplFontCharMap->mpRangeCodes[0] )
            return false;
        bHasChar = ((nRange & 1) == 0); // inside a range
    } else { // glyph mapping is available
        const int nGlyphIndex = GetGlyphIndex( cChar );
        bHasChar = (nGlyphIndex != 0); // not the notdef-glyph
    }

    return bHasChar;
}

sal_UCS4 FontCharMap::GetFirstChar() const
{
    return mpImplFontCharMap->mpRangeCodes[0];
}

sal_UCS4 FontCharMap::GetLastChar() const
{
    return (mpImplFontCharMap->mpRangeCodes[ 2*mpImplFontCharMap->mnRangeCount-1 ] - 1);
}

sal_UCS4 FontCharMap::GetNextChar( sal_UCS4 cChar ) const
{
    if( cChar < GetFirstChar() )
        return GetFirstChar();
    if( cChar >= GetLastChar() )
        return GetLastChar();

    int nRange = findRangeIndex( cChar + 1 );
    if( nRange & 1 )                       // outside of range?
        return mpImplFontCharMap->mpRangeCodes[ nRange + 1 ]; // => first in next range
    return (cChar + 1);
}

sal_UCS4 FontCharMap::GetPrevChar( sal_UCS4 cChar ) const
{
    if( cChar <= GetFirstChar() )
        return GetFirstChar();
    if( cChar > GetLastChar() )
        return GetLastChar();

    int nRange = findRangeIndex( cChar - 1 );
    if( nRange & 1 )                            // outside a range?
        return (mpImplFontCharMap->mpRangeCodes[ nRange ] - 1);    // => last in prev range
    return (cChar - 1);
}

int FontCharMap::GetIndexFromChar( sal_UCS4 cChar ) const
{
    // TODO: improve linear walk?
    int nCharIndex = 0;
    const sal_UCS4* pRange = &mpImplFontCharMap->mpRangeCodes[0];
    for( int i = 0; i < mpImplFontCharMap->mnRangeCount; ++i )
    {
        sal_UCS4 cFirst = *(pRange++);
        sal_UCS4 cLast  = *(pRange++);
        if( cChar >= cLast )
            nCharIndex += cLast - cFirst;
        else if( cChar >= cFirst )
            return nCharIndex + (cChar - cFirst);
        else
            break;
    }

    return -1;
}

sal_UCS4 FontCharMap::GetCharFromIndex( int nIndex ) const
{
    // TODO: improve linear walk?
    const sal_UCS4* pRange = &mpImplFontCharMap->mpRangeCodes[0];
    for( int i = 0; i < mpImplFontCharMap->mnRangeCount; ++i )
    {
        sal_UCS4 cFirst = *(pRange++);
        sal_UCS4 cLast  = *(pRange++);
        nIndex -= cLast - cFirst;
        if( nIndex < 0 )
            return (cLast + nIndex);
    }

    // we can only get here with an out-of-bounds charindex
    return mpImplFontCharMap->mpRangeCodes[0];
}

int FontCharMap::findRangeIndex( sal_UCS4 cChar ) const
{
    int nLower = 0;
    int nMid   = mpImplFontCharMap->mnRangeCount;
    int nUpper = 2 * mpImplFontCharMap->mnRangeCount - 1;
    while( nLower < nUpper )
    {
        if( cChar >= mpImplFontCharMap->mpRangeCodes[ nMid ] )
            nLower = nMid;
        else
            nUpper = nMid - 1;
        nMid = (nLower + nUpper + 1) / 2;
    }

    return nMid;
}

int FontCharMap::GetGlyphIndex( sal_UCS4 cChar ) const
{
    // return -1 if the object doesn't know the glyph ids
    if( !mpImplFontCharMap->mpStartGlyphs )
        return -1;

    // return 0 if the unicode doesn't have a matching glyph
    int nRange = findRangeIndex( cChar );
    // check that we are inside any range
    if( (nRange == 0) && (cChar < mpImplFontCharMap->mpRangeCodes[0]) ) {
        // symbol aliasing gives symbol fonts a second chance
        const bool bSymbolic = cChar <= 0xFF && (mpImplFontCharMap->mpRangeCodes[0]>=0xF000) &&
                                                (mpImplFontCharMap->mpRangeCodes[1]<=0xF0FF);
        if( !bSymbolic )
            return 0;
        // check for symbol aliasing (U+F0xx -> U+00xx)
        cChar |= 0xF000;
        nRange = findRangeIndex( cChar );
        if( (nRange == 0) && (cChar < mpImplFontCharMap->mpRangeCodes[0]) ) {
            return 0;
        }
    }
    // check that we are inside a range
    if( (nRange & 1) != 0 )
        return 0;

    // get glyph index directly or indirectly
    int nGlyphIndex = cChar - mpImplFontCharMap->mpRangeCodes[ nRange ];
    const int nStartIndex = mpImplFontCharMap->mpStartGlyphs[ nRange/2 ];
    if( nStartIndex >= 0 ) {
        // the glyph index can be calculated
        nGlyphIndex += nStartIndex;
    } else {
        // the glyphid array has the glyph index
        nGlyphIndex = mpImplFontCharMap->mpGlyphIds[ nGlyphIndex - nStartIndex];
    }

    return nGlyphIndex;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
