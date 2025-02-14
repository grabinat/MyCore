/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <test/bootstrapfixture.hxx>
#include <unotest/macros_test.hxx>

#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/text/XPageCursor.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/style/XStyleFamiliesSupplier.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/drawing/XDrawPageSupplier.hpp>
#include <com/sun/star/table/BorderLine2.hpp>

using namespace ::com::sun::star;

namespace
{
/// Tests for writerfilter/source/dmapper/PropertyMap.cxx.
class Test : public test::BootstrapFixture, public unotest::MacrosTest
{
private:
    uno::Reference<lang::XComponent> mxComponent;

public:
    void setUp() override;
    void tearDown() override;
    uno::Reference<lang::XComponent>& getComponent() { return mxComponent; }
};

void Test::setUp()
{
    test::BootstrapFixture::setUp();

    mxDesktop.set(frame::Desktop::create(mxComponentContext));
}

void Test::tearDown()
{
    if (mxComponent.is())
        mxComponent->dispose();

    test::BootstrapFixture::tearDown();
}

constexpr OUStringLiteral DATA_DIRECTORY = u"/writerfilter/qa/cppunittests/dmapper/data/";

CPPUNIT_TEST_FIXTURE(Test, testFloatingTableHeader)
{
    OUString aURL = m_directories.getURLFromSrc(DATA_DIRECTORY) + "floating-table-header.docx";
    getComponent() = loadFromDesktop(aURL);
    uno::Reference<frame::XModel> xModel(getComponent(), uno::UNO_QUERY);
    uno::Reference<text::XTextViewCursorSupplier> xTextViewCursorSupplier(
        xModel->getCurrentController(), uno::UNO_QUERY);
    uno::Reference<text::XPageCursor> xCursor(xTextViewCursorSupplier->getViewCursor(),
                                              uno::UNO_QUERY);
    xCursor->jumpToLastPage();
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 1
    // - Actual  : 3
    // i.e. a document which is 1 page in Word was imported as a 3 page one.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int16>(1), xCursor->getPage());
}

CPPUNIT_TEST_FIXTURE(Test, testFollowPageTopMargin)
{
    // Load a document with 2 pages: first page has larger top margin, second page has smaller top
    // margin.
    OUString aURL = m_directories.getURLFromSrc(DATA_DIRECTORY) + "follow-page-top-margin.docx";
    getComponent() = loadFromDesktop(aURL);
    uno::Reference<style::XStyleFamiliesSupplier> xStyleFamiliesSupplier(getComponent(),
                                                                         uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xStyleFamilies
        = xStyleFamiliesSupplier->getStyleFamilies();
    uno::Reference<container::XNameAccess> xStyleFamily(xStyleFamilies->getByName("PageStyles"),
                                                        uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xStyle(xStyleFamily->getByName("Standard"), uno::UNO_QUERY);
    auto nTopMargin = xStyle->getPropertyValue("TopMargin").get<sal_Int32>();

    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 250
    // - Actual  : 1249
    // i.e. the top margin on page 2 was too large.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(250), nTopMargin);
}

CPPUNIT_TEST_FIXTURE(Test, testTableNegativeVerticalPos)
{
    // Given a document with a table which has a negative vertical position (moves up to overlap
    // with the header):
    OUString aURL
        = m_directories.getURLFromSrc(DATA_DIRECTORY) + "table-negative-vertical-pos.docx";

    // When loading that document:
    getComponent() = loadFromDesktop(aURL);

    // Then make sure we don't import that as a plain table, which can't have a negative top margin:
    uno::Reference<drawing::XDrawPageSupplier> xDrawPageSupplier(getComponent(), uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage = xDrawPageSupplier->getDrawPage();
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 1
    // - Actual  : 0
    // i.e. this was imported as a plain table, resulting in a 0 top margin (y pos too large).
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), xDrawPage->getCount());
}

CPPUNIT_TEST_FIXTURE(Test, testNegativePageBorder)
{
    // Given a document with a top margin and a border which has more spacing than the margin:
    OUString aURL = m_directories.getURLFromSrc(DATA_DIRECTORY) + "negative-page-border.docx";

    // When loading that document:
    getComponent() = loadFromDesktop(aURL);

    // Then make sure that the border distance is negative, so it can appear at the correct
    // position:
    uno::Reference<style::XStyleFamiliesSupplier> xStyleFamiliesSupplier(getComponent(),
                                                                         uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xStyleFamilies
        = xStyleFamiliesSupplier->getStyleFamilies();
    uno::Reference<container::XNameAccess> xStyleFamily(xStyleFamilies->getByName("PageStyles"),
                                                        uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xStyle(xStyleFamily->getByName("Standard"), uno::UNO_QUERY);
    auto nTopMargin = xStyle->getPropertyValue("TopMargin").get<sal_Int32>();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(501), nTopMargin);
    auto aTopBorder = xStyle->getPropertyValue("TopBorder").get<table::BorderLine2>();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt32>(159), aTopBorder.LineWidth);
    auto nTopBorderDistance = xStyle->getPropertyValue("TopBorderDistance").get<sal_Int32>();
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: -646
    // - Actual  : 0
    // i.e. the border negative distance was lost.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(-646), nTopBorderDistance);
}

CPPUNIT_TEST_FIXTURE(Test, testNegativePageBorderNoMargin)
{
    // Given a document with no top margin and a border which has spacing:
    OUString aURL
        = m_directories.getURLFromSrc(DATA_DIRECTORY) + "negative-page-border-no-margin.docx";

    // When loading that document:
    getComponent() = loadFromDesktop(aURL);

    // Then make sure that the border distance is negative, so it can appear at the correct
    // position:
    uno::Reference<style::XStyleFamiliesSupplier> xStyleFamiliesSupplier(getComponent(),
                                                                         uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xStyleFamilies
        = xStyleFamiliesSupplier->getStyleFamilies();
    uno::Reference<container::XNameAccess> xStyleFamily(xStyleFamilies->getByName("PageStyles"),
                                                        uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xStyle(xStyleFamily->getByName("Standard"), uno::UNO_QUERY);
    auto nTopMargin = xStyle->getPropertyValue("TopMargin").get<sal_Int32>();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(0), nTopMargin);
    auto aTopBorder = xStyle->getPropertyValue("TopBorder").get<table::BorderLine2>();
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_uInt32>(159), aTopBorder.LineWidth);
    auto nTopBorderDistance = xStyle->getPropertyValue("TopBorderDistance").get<sal_Int32>();
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: -1147
    // - Actual  : 0
    // i.e. the border negative distance was lost.
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(-1147), nTopBorderDistance);
}
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
