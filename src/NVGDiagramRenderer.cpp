#include "stdafx.h"
#include "NVGDiagramRenderer.h"

#include <array>
#include <filesystem>
#include <map>

#include "mmcore/CoreInstance.h"
#include "mmcore/misc/PngBitmapCodec.h"
#include "mmcore/param/IntParam.h"
#include "mmcore/param/StringParam.h"
#include "mmcore/param/EnumParam.h"
#include "mmcore/param/FlexEnumParam.h"
#include "mmcore/param/ButtonParam.h"
#include "mmcore/param/BoolParam.h"
#include "mmcore/param/FloatParam.h"
#include "mmcore/utility/ColourParser.h"
#include "mmcore/utility/ResourceWrapper.h"

#include "vislib/graphics/gl/IncludeAllGL.h"
#include "vislib/graphics/gl/Verdana.inc"
#include "vislib/graphics/gl/SimpleFont.h"
#include "vislib/math/Rectangle.h"
#include "vislib/math/ShallowPoint.h"
#include "vislib/math/ShallowMatrix.h"
#include "vislib/sys/BufferedFile.h"
#include "vislib/sys/sysfunctions.h"

#include <GL/glu.h>

#include <math.h>
#include <float.h>

#include "TraceInfoCall.h"
#include "mmcore/view/CallGetTransferFunction.h"

#include "nanovg.h"
#define NANOVG_GL3_IMPLEMENTATION
#include "nanovg_gl.h"

#define BLENDISH_IMPLEMENTATION
#include "blendish.h"

const GLuint SSBObindingPoint = 2;

//#include "oui.h"

using namespace megamol;
using namespace megamol::core;
using namespace megamol::infovis;
using namespace megamol::stdplugin::datatools;
using namespace megamol::stdplugin::datatools::floattable;
using namespace vislib::graphics::gl;
using vislib::sys::Log;

/*
 * NVGDiagramRenderer::NVGDiagramRenderer (CTOR)
 */
NVGDiagramRenderer::NVGDiagramRenderer(void) : Renderer2DModule(),
    dataCallerSlot("getData", "Connects the diagram rendering with data storage."),
    selectionCallerSlot("getSelection", "Connects the diagram rendering with selection storage."),
    hiddenCallerSlot("getHidden", "Connects the diagram rendering with visibility storage."),
    theFont(FontInfo_Verdana), decorationDepth(0.0f),
    diagramTypeParam("Type", "The diagram type."),
    diagramStyleParam("Style", "The diagram style."),
    numXTicksParam("X Ticks", "The number of X ticks."),
    numYTicksParam("Y Ticks", "The number of Y ticks."),
    lineWidthParam("linewidth", "width of the drawn lines."),
    drawYLogParam("logarithmic", "scale the Y axis logarithmically."),
    foregroundColorParam("foregroundCol", "The color of the diagram decorations."),
    drawCategoricalParam("categorical", "Draw column charts as categorical."),
    showCrosshairParam("show XHair", "bool param for the Crosshair toggle"),
    showCrosshairToggleParam("toggle XHair", "Show a crosshair to inform the user of the current mouse position."),
    showAllParam("show all", "Make all series visible."),
    hideAllParam("hide all", "Make all series invisible."),
    showGuidesParam("show guides", "Show defined guides in the diagram."),
    autoAspectParam("auto aspect", "Automatically adjust aspect ratio to fit especially bar data."),
    aspectRatioParam("acpect ratio", "Aspect ratio for the diagram."),
    showMarkersParam("show markers", "When to show markers in line charts."),
    preparedData(NULL), categories(vislib::Array<vislib::StringA>()),
    /*hoveredMarker(NULL),*/
    hoveredSeries(0), /*diagram(NULL), selectionCall(NULL), hiddenCall(NULL),*/
    markerTextures(), preparedSeries(), localXIndexToGlobal(), xAxis(0.0f), yAxis(0.0f),
    xTickOff(0.0f), barWidth(0.0f), fontSize(1.0f / 20.0f),
    legendOffset(0.0f), legendWidth(0.0f), legendHeight(0.0f), legendMargin(0.0f),
    barWidthRatio(0.8f), /*selectedSeries(NULL),*/
    unselectedColor(vislib::math::Vector<float, 4>(0.5f, 0.5f, 0.5f, 1.0f)),
    //hovering(false),
    hoverPoint(), seriesVisible(),
    inputHash((std::numeric_limits<size_t>::max)()), myHash(0),
    getSelectorsSlot("getSelectors", "Slot asking for selected columns"),
    abcissaSelectorSlot("abcissaSelector", "Slot to select column as abcissa"),
    abcissaIdx(0),
    screenSpaceCanvasOffsetParam("screenSpaceCanvasOffset", "Slot defining screen space canvas margin"),
    currBuf(0), bufSize(32 * 1024 * 1024), numBuffers(3),
    singleBufferCreationBits(GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT),
    singleBufferMappingBits(GL_MAP_PERSISTENT_BIT | GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT),
    fences(),
    alphaScalingParam("alphaScaling", "scaling factor for particle alpha"),
    attenuateSubpixelParam("attenuateSubpixel", "attenuate alpha of points that should have subpixel size"),
    mouseRightPressed(false), mouseX(0), mouseY(0),
    metaRequestSlot("metaRequest", "Slot for metat requests"),
    transFuncSlot("transFunc", "Slot for transfer function"),
    colSelectParam("colSelect", "Param to select color column"),
    descSelectParam("descSelect", "Param to select desc column") {

    // segmentation data caller slot
    this->dataCallerSlot.SetCompatibleCall<CallFloatTableDataDescription>();
    //this->dataCallerSlot.SetCompatibleCall<protein_calls::DiagramCallDescription>();
    this->MakeSlotAvailable(&this->dataCallerSlot);

    this->getSelectorsSlot.SetCompatibleCall<DiagramSeriesCallDescription>();
    this->MakeSlotAvailable(&this->getSelectorsSlot);

    core::param::FlexEnumParam *abcissaSelectorEP = new core::param::FlexEnumParam("undef");
    this->abcissaSelectorSlot << abcissaSelectorEP;
    this->MakeSlotAvailable(&this->abcissaSelectorSlot);

    core::param::FlexEnumParam *colorSelectorEP = new core::param::FlexEnumParam("undef");
    this->colSelectParam << colorSelectorEP;
    this->MakeSlotAvailable(&this->colSelectParam);

    core::param::FlexEnumParam *descSelectorEP = new core::param::FlexEnumParam("undef");
    this->descSelectParam << descSelectorEP;
    this->MakeSlotAvailable(&this->descSelectParam);

    this->screenSpaceCanvasOffsetParam << new core::param::FloatParam(0.9f, (std::numeric_limits<float>::min)(), 1.0f);
    this->MakeSlotAvailable(&this->screenSpaceCanvasOffsetParam);

    this->fences.resize(numBuffers);

    this->alphaScalingParam << new core::param::FloatParam(1.0f);
    this->MakeSlotAvailable(&this->alphaScalingParam);

    this->attenuateSubpixelParam << new core::param::BoolParam(false);
    this->MakeSlotAvailable(&this->attenuateSubpixelParam);

    this->metaRequestSlot.SetCompatibleCall<TraceInfoCallDescription>();
    this->MakeSlotAvailable(&this->metaRequestSlot);

    this->transFuncSlot.SetCompatibleCall<view::CallGetTransferFunctionDescription>();
    this->MakeSlotAvailable(&this->transFuncSlot);

    /*this->selectionCallerSlot.SetCompatibleCall<protein_calls::IntSelectionCallDescription>();
    this->MakeSlotAvailable(&this->selectionCallerSlot);

    this->hiddenCallerSlot.SetCompatibleCall<protein_calls::IntSelectionCallDescription>();
    this->MakeSlotAvailable(&this->hiddenCallerSlot);*/

    param::EnumParam *dt = new param::EnumParam(0);
    dt->SetTypePair(DIAGRAM_TYPE_LINE, "Line");
    dt->SetTypePair(DIAGRAM_TYPE_LINE_STACKED, "Stacked Line");
    dt->SetTypePair(DIAGRAM_TYPE_LINE_STACKED_NORMALIZED, "100% Stacked Line");
    dt->SetTypePair(DIAGRAM_TYPE_COLUMN, "Clustered Column");
    dt->SetTypePair(DIAGRAM_TYPE_COLUMN_STACKED, "Stacked Column");
    dt->SetTypePair(DIAGRAM_TYPE_COLUMN_STACKED_NORMALIZED, "100% Stacked Column");
    dt->SetTypePair(DIAGRAM_TYPE_POINT_SPLATS, "Point Splats");
    this->diagramTypeParam.SetParameter(dt);
    this->MakeSlotAvailable(&this->diagramTypeParam);

    param::EnumParam *ds = new param::EnumParam(0);
    ds->SetTypePair(DIAGRAM_STYLE_WIRE, "Wireframe");
    ds->SetTypePair(DIAGRAM_STYLE_FILLED, "Filled");
    this->diagramStyleParam.SetParameter(ds);
    this->MakeSlotAvailable(&this->diagramStyleParam);

    this->numXTicksParam.SetParameter(new param::IntParam(4, 3, 100));
    this->MakeSlotAvailable(&this->numXTicksParam);
    this->numYTicksParam.SetParameter(new param::IntParam(4, 3, 100));
    this->MakeSlotAvailable(&this->numYTicksParam);
    this->drawYLogParam.SetParameter(new param::BoolParam(false));
    this->MakeSlotAvailable(&this->drawYLogParam);
    this->lineWidthParam.SetParameter(new param::FloatParam(1.0f, 0.0001f, 10000.0f));
    this->MakeSlotAvailable(&this->lineWidthParam);

    this->foregroundColorParam.SetParameter(new param::StringParam("white"));
    this->fgColor.Set(1.0f, 1.0f, 1.0f, 1.0f);
    this->MakeSlotAvailable(&this->foregroundColorParam);

    param::EnumParam *dc = new param::EnumParam(0);
    dc->SetTypePair(1, "true");
    dc->SetTypePair(0, "false");
    this->drawCategoricalParam.SetParameter(dc);
    this->MakeSlotAvailable(&this->drawCategoricalParam);

    param::EnumParam *sm = new param::EnumParam(0);
    sm->SetTypePair(DIAGRAM_MARKERS_SHOW_ALL, "show all");
    sm->SetTypePair(DIAGRAM_MARKERS_SHOW_SELECTED, "show selected");
    sm->SetTypePair(DIAGRAM_MARKERS_SHOW_NONE, "hide all");
    this->showMarkersParam.SetParameter(sm);
    this->MakeSlotAvailable(&this->showMarkersParam);

    this->showCrosshairParam.SetParameter(new param::BoolParam(false));
    this->MakeSlotAvailable(&this->showCrosshairParam);
    this->showCrosshairToggleParam.SetParameter(new param::ButtonParam(vislib::sys::KeyCode::KEY_MOD_CTRL + 'c'));
    this->showCrosshairToggleParam.SetUpdateCallback(this, &NVGDiagramRenderer::onCrosshairToggleButton);
    this->MakeSlotAvailable(&this->showCrosshairToggleParam);
    this->showAllParam.SetParameter(new param::ButtonParam(vislib::sys::KeyCode::KEY_MOD_CTRL + 's'));
    this->showAllParam.SetUpdateCallback(this, &NVGDiagramRenderer::onShowAllButton);
    this->MakeSlotAvailable(&this->showAllParam);
    this->hideAllParam.SetParameter(new param::ButtonParam(vislib::sys::KeyCode::KEY_MOD_CTRL + 'h'));
    this->hideAllParam.SetUpdateCallback(this, &NVGDiagramRenderer::onHideAllButton);
    this->MakeSlotAvailable(&this->hideAllParam);

    this->aspectRatioParam.SetParameter(new param::FloatParam(1.0, 0.0));
    this->MakeSlotAvailable(&this->aspectRatioParam);
    this->autoAspectParam.SetParameter(new param::BoolParam(false));
    this->MakeSlotAvailable(&this->autoAspectParam);
    this->showGuidesParam.SetParameter(new param::BoolParam(true));
    this->MakeSlotAvailable(&this->showGuidesParam);

    seriesVisible.AssertCapacity(100);
    seriesVisible.SetCapacityIncrement(100);
}

    void NVGDiagramRenderer::lockSingle(GLsync& syncObj) {
        if (syncObj) {
            glDeleteSync(syncObj);
        }
        syncObj = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }

    void NVGDiagramRenderer::waitSingle(GLsync& syncObj) {
        if (syncObj) {
            while (1) {
                GLenum wait = glClientWaitSync(syncObj, GL_SYNC_FLUSH_COMMANDS_BIT, 1);
                if (wait == GL_ALREADY_SIGNALED || wait == GL_CONDITION_SATISFIED) {
                    return;
                }
            }
        }
    }

/*
 * Diagram2DRenderer::~Diagram2DRenderer (DTOR)
 */
NVGDiagramRenderer::~NVGDiagramRenderer(void) {
    this->Release();
}

/*
 * Diagram2DRenderer::create
 */
bool NVGDiagramRenderer::create() {

    /*this->LoadIcon("plop.png", protein_calls::DiagramCall::DIAGRAM_MARKER_DISAPPEAR);
    this->LoadIcon("bookmark.png", protein_calls::DiagramCall::DIAGRAM_MARKER_BOOKMARK);
    this->LoadIcon("merge.png", protein_calls::DiagramCall::DIAGRAM_MARKER_MERGE);
    this->LoadIcon("split.png", protein_calls::DiagramCall::DIAGRAM_MARKER_SPLIT);
    this->LoadIcon("exit2.png", protein_calls::DiagramCall::DIAGRAM_MARKER_EXIT);*/

    this->fpsicb = std::bind(&NVGDiagramRenderer::seriesInsertionCB, this, std::placeholders::_1);

    this->nvgCtxt = nvgCreateGL3(NVG_ANTIALIAS | NVG_STENCIL_STROKES | NVG_DEBUG);
    if (this->nvgCtxt == nullptr) {
        Log::DefaultLog.WriteError("NVGDiagramRenderer: Could not init NanoVG\n");
        return false;
    }

    auto rw = core::utility::ResourceWrapper();
    char *filepaths = NULL;
    size_t outSize = rw.LoadTextResource(this->GetCoreInstance()->Configuration(), vislib::StringA("infovis_filepaths.txt"), &filepaths);

    if (filepaths == NULL) {
        return NULL;
    }

    vislib::StringA font_path;
    vislib::StringA icon_path;

    auto tokenizer = vislib::StringTokeniserA(filepaths, "\n");
    if (tokenizer.HasNext()) {
        font_path = tokenizer.Next();
        font_path.Trim(" \r");
    } else {
        return false;
    }

    if (tokenizer.HasNext()) {
        icon_path = tokenizer.Next();
        icon_path.Trim(" \r");
    } else {
        return false;
    }

    //this->nvgFontSans = nvgCreateFont((NVGcontext*)this->nvgCtxt, "sans", "T:\\Programmierung\\megamol2015\\infovis\\3rd\\oui-blendish\\DejaVuSans.ttf");
    this->nvgFontSans = nvgCreateFont((NVGcontext*)this->nvgCtxt, "sans", font_path);
    bndSetFont(this->nvgFontSans);
    bndSetIconImage(nvgCreateImage((NVGcontext*)this->nvgCtxt, icon_path, 0));

    glGenBuffers(1, &this->theSingleBuffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, this->theSingleBuffer);
    glBufferStorage(GL_SHADER_STORAGE_BUFFER, this->bufSize * this->numBuffers, nullptr, singleBufferCreationBits);
    this->theSingleMappedMem = glMapNamedBufferRangeEXT(this->theSingleBuffer, 0, this->bufSize * this->numBuffers, singleBufferMappingBits);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glBindVertexArray(0);

    this->newShader = std::make_shared<vislib::graphics::gl::GLSLShader>(vislib::graphics::gl::GLSLShader());
    /*this->newShader->CreateFromFile("T:\\Programmierung\\megamol2015\\infovis\\Shaders\\nvgdr_splat_v.glsl",
        "T:\\Programmierung\\megamol2015\\infovis\\Shaders\\nvgdr_splat_f.glsl");*/

    char *vShader = NULL;
    size_t vShaderS = 0;
    char *fShader = NULL;
    size_t fShaderS = 0;

    vShaderS = rw.LoadTextResource(this->GetCoreInstance()->Configuration(), "nvgdr_splat_v.glsl", &vShader);
    fShaderS = rw.LoadTextResource(this->GetCoreInstance()->Configuration(), "nvgdr_splat_f.glsl", &fShader);

    try {
        /*if (!this->newShader->CreateFromFile("T:\\Programmierung\\megamol2015\\infovis\\Shaders\\nvgdr_splat_v.glsl",
            "T:\\Programmierung\\megamol2015\\infovis\\Shaders\\nvgdr_splat_f.glsl")) {
            vislib::sys::Log::DefaultLog.WriteMsg(vislib::sys::Log::LEVEL_ERROR,
                "Unable to compile sphere shader: Unknown error\n");
            return false;
        }*/

        if (!this->newShader->Create(vShader, fShader)) {
            vislib::sys::Log::DefaultLog.WriteMsg(vislib::sys::Log::LEVEL_ERROR,
                "Unable to compile sphere shader: Unknown error\n");
            return false;
        }

    } catch (vislib::graphics::gl::AbstractOpenGLShader::CompileException ce) {
        vislib::sys::Log::DefaultLog.WriteMsg(vislib::sys::Log::LEVEL_ERROR,
            "Unable to compile sphere shader (@%s): %s\n",
            vislib::graphics::gl::AbstractOpenGLShader::CompileException::CompileActionName(
            ce.FailedAction()), ce.GetMsgA());
        return nullptr;
    } catch (vislib::Exception e) {
        vislib::sys::Log::DefaultLog.WriteMsg(vislib::sys::Log::LEVEL_ERROR,
            "Unable to compile sphere shader: %s\n", e.GetMsgA());
        return nullptr;
    } catch (...) {
        vislib::sys::Log::DefaultLog.WriteMsg(vislib::sys::Log::LEVEL_ERROR,
            "Unable to compile sphere shader: Unknown exception\n");
        return nullptr;
    }

    return true;
}

/*
 * Diagram2DRenderer::release
 */
void NVGDiagramRenderer::release() { }

bool NVGDiagramRenderer::assertData(floattable::CallFloatTableData * ft) {
    this->floatTable = ft;

    if (!updateColumnSelectors()) return false;

    if (this->inputHash == ft->DataHash() && !isAnythingDirty()) return true;

    if (this->inputHash != ft->DataHash()) {
        // gather columninfo
        this->abcissaSelectorSlot.Param<core::param::FlexEnumParam>()->ClearValues();
        this->colSelectParam.Param<core::param::FlexEnumParam>()->ClearValues();
        this->descSelectParam.Param<core::param::FlexEnumParam>()->ClearValues();
        for (size_t i = 0; i < ft->GetColumnsCount(); i++) {
            this->abcissaSelectorSlot.Param<core::param::FlexEnumParam>()->AddValue(ft->GetColumnsInfos()[i].Name());
            this->colSelectParam.Param<core::param::FlexEnumParam>()->AddValue(ft->GetColumnsInfos()[i].Name());
            this->descSelectParam.Param<core::param::FlexEnumParam>()->AddValue(ft->GetColumnsInfos()[i].Name());
        }
    }

    auto colname = this->abcissaSelectorSlot.Param<core::param::FlexEnumParam>()->Value();
    this->abcissaIdx = 0;
    for (size_t i = 0; i < ft->GetColumnsCount(); i++) {
        if (ft->GetColumnsInfos()[i].Name().compare(colname) == 0)
            this->abcissaIdx = i;
    }

    colname = this->colSelectParam.Param<core::param::FlexEnumParam>()->Value();
    this->colorColIdx = 0;
    for (size_t i = 0; i < ft->GetColumnsCount(); i++) {
        if (ft->GetColumnsInfos()[i].Name().compare(colname) == 0) {
            this->colorColIdx = i;
            this->minMaxColorCol.Set(ft->GetColumnsInfos()[i].MinimumValue(), ft->GetColumnsInfos()[i].MaximumValue());
        }
    }

    colname = this->descSelectParam.Param<core::param::FlexEnumParam>()->Value();
    this->descColIdx = 0;
    for (size_t i = 0; i < ft->GetColumnsCount(); i++) {
        if (ft->GetColumnsInfos()[i].Name().compare(colname) == 0) {
            this->descColIdx = i;
        }
    }

    /*if (this->inputHash != ft->DataHash() || isAnythingDirty()) {
        this->prepareData(false, false, false);
    }*/

    this->myHash++;
    this->inputHash = ft->DataHash();
    this->resetDirtyFlags();

    return true;
}


void NVGDiagramRenderer::defineLayout(float w, float h) {
    this->screenSpaceMidPoint.Set(w / 2, h / 2);

    float screenSpaceCanvasOffset = this->screenSpaceCanvasOffsetParam.Param<core::param::FloatParam>()->Value();

    this->screenSpaceCanvasSize.Set(this->screenSpaceMidPoint.GetX()*screenSpaceCanvasOffset,
        this->screenSpaceMidPoint.GetY()*screenSpaceCanvasOffset);

    if (this->screenSpaceCanvasSize.GetWidth() < this->screenSpaceCanvasSize.GetHeight()) {
        uint32_t tmp = this->screenSpaceCanvasSize.GetWidth()*screenSpaceCanvasOffset;
        this->screenSpaceDiagramSize.Set(tmp, tmp);
    } else {
        uint32_t tmp = this->screenSpaceCanvasSize.GetHeight()*screenSpaceCanvasOffset;
        this->screenSpaceDiagramSize.Set(tmp, tmp);
    }
}


void NVGDiagramRenderer::drawPointSplats(float w, float h) {
    this->defineLayout(w, h);
    float dw = this->screenSpaceDiagramSize.GetWidth();
    float dh = this->screenSpaceDiagramSize.GetHeight();
    float mw = this->screenSpaceMidPoint.GetX();
    float mh = this->screenSpaceMidPoint.GetY();

    this->drawXAxis(DIAGRAM_XAXIS_FLOAT);
    this->drawYAxis();

    /*this->showToolTip(500, 500,
        std::string("symbol"), std::string("module"), std::string("file"), size_t(1), size_t(2), size_t(3));*/

    // TODO Set this!!!
    view::CallRender2D *cr = this->callR2D;
    float scaling = 1.0f;

    size_t idx = 0;
    if (this->mouseRightPressed) {
        idx = searchAndDispPointAttr(this->mouseX, this->mouseY);
    }

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);
    glEnable(GL_POINT_SPRITE);

    float viewportStuff[4];
    ::glGetFloatv(GL_VIEWPORT, viewportStuff);
    glPointSize(vislib::math::Max(viewportStuff[2], viewportStuff[3]));
    if (viewportStuff[2] < 1.0f) viewportStuff[2] = 1.0f;
    if (viewportStuff[3] < 1.0f) viewportStuff[3] = 1.0f;
    viewportStuff[2] = 2.0f / viewportStuff[2];
    viewportStuff[3] = 2.0f / viewportStuff[3];

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, theSingleBuffer);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBObindingPoint, this->theSingleBuffer);

    // this is the apex of suck and must die
    GLfloat modelViewMatrix_column[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, modelViewMatrix_column);
    vislib::math::ShallowMatrix<GLfloat, 4, vislib::math::COLUMN_MAJOR> modelViewMatrix(&modelViewMatrix_column[0]);
    vislib::math::Matrix<GLfloat, 4, vislib::math::COLUMN_MAJOR> scaleMat;
    scaleMat.SetAt(0, 0, scaling);
    scaleMat.SetAt(1, 1, scaling);
    scaleMat.SetAt(2, 2, scaling);
    //modelViewMatrix = modelViewMatrix * scaleMat;
    GLfloat projMatrix_column[16];
    glGetFloatv(GL_PROJECTION_MATRIX, projMatrix_column);
    vislib::math::ShallowMatrix<GLfloat, 4, vislib::math::COLUMN_MAJOR> projMatrix(&projMatrix_column[0]);
    // Compute modelviewprojection matrix
    vislib::math::Matrix<GLfloat, 4, vislib::math::COLUMN_MAJOR> modelViewMatrixInv = modelViewMatrix;
    modelViewMatrixInv.Invert();
    vislib::math::Matrix<GLfloat, 4, vislib::math::COLUMN_MAJOR> modelViewProjMatrix = projMatrix * modelViewMatrix;
    // end suck

    newShader->Enable();
    //colIdxAttribLoc = glGetAttribLocation(*this->newShader, "colIdx");
    glUniform4fv(newShader->ParameterLocation("viewAttr"), 1, viewportStuff);
    //glUniform3fv(newShader->ParameterLocation("camIn"), 1, cr->GetCameraParameters()->Front().PeekComponents());
    //glUniform3fv(newShader->ParameterLocation("camRight"), 1, cr->GetCameraParameters()->Right().PeekComponents());
    //glUniform3fv(newShader->ParameterLocation("camUp"), 1, cr->GetCameraParameters()->Up().PeekComponents());
    //glUniform4fv(newShader->ParameterLocation("clipDat"), 1, clipDat);
    //glUniform4fv(newShader->ParameterLocation("clipCol"), 1, clipCol);
    glUniform1f(newShader->ParameterLocation("scaling"), scaling);
    glUniform1f(newShader->ParameterLocation("alphaScaling"), this->alphaScalingParam.Param<param::FloatParam>()->Value());
    glUniform1i(newShader->ParameterLocation("attenuateSubpixel"), this->attenuateSubpixelParam.Param<param::BoolParam>()->Value() ? 1 : 0);
    //glUniform1f(newShader->ParameterLocation("zNear"), cr->GetCameraParameters()->NearClip());
    glUniformMatrix4fv(newShader->ParameterLocation("modelViewProjection"), 1, GL_FALSE, modelViewProjMatrix.PeekComponents());
    glUniformMatrix4fv(newShader->ParameterLocation("modelViewInverse"), 1, GL_FALSE, modelViewMatrixInv.PeekComponents());
    glUniformMatrix4fv(newShader->ParameterLocation("modelView"), 1, GL_FALSE, modelViewMatrix.PeekComponents());
    glUniform1ui(newShader->ParameterLocation("pointIdx"), idx);
    glUniform1i(newShader->ParameterLocation("pik"), this->mouseRightPressed ? 1:0);

    unsigned int colTabSize = 0;
    view::CallGetTransferFunction *cgtf = this->transFuncSlot.CallAs<view::CallGetTransferFunction>();
    glEnable(GL_TEXTURE_1D);
    glActiveTexture(GL_TEXTURE0);
    if ((cgtf != NULL) && ((*cgtf)())) {
        glBindTexture(GL_TEXTURE_1D, cgtf->OpenGLTexture());
        colTabSize = cgtf->TextureSize();
    }/* else {
        glBindTexture(GL_TEXTURE_1D, this->greyTF);
        colTabSize = 2;
    }*/
    glUniform1i(this->newShader->ParameterLocation("colTab"), 0);

    glUniform4f(this->newShader->ParameterLocation("inConsts1"), this->lineWidthParam.Param<core::param::FloatParam>()->Value(),
        this->minMaxColorCol.GetWidth(), this->minMaxColorCol.GetHeight(), colTabSize);


    // drawarrays
    size_t vertCounter = 0;
    size_t numVerts = this->bufSize / (3.0f * sizeof(float));
    const char *currVert = reinterpret_cast<const char *>(this->pointData.data());
    size_t numEntries = this->pointData.size() / 3;
    while (vertCounter < numEntries) {
        void *mem = static_cast<char*>(theSingleMappedMem) + this->bufSize * this->currBuf;
        size_t vertsThisTime = vislib::math::Min(numEntries - vertCounter, numVerts);
        this->waitSingle(fences[currBuf]);
        memcpy(mem, currVert, vertsThisTime*3 * sizeof(float));
        glFlushMappedNamedBufferRangeEXT(theSingleBuffer, bufSize * currBuf, vertsThisTime*3 * sizeof(float));
        //glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT);
        //glUniform1i(this->newShader->ParameterLocation("instanceOffset"), numVerts * currBuf);
        glUniform1i(this->newShader->ParameterLocation("instanceOffset"), 0);
        glUniform1ui(this->newShader->ParameterLocation("idxOffset"), numVerts * currBuf);

        //this->setPointers(parts, this->theSingleBuffer, reinterpret_cast<const void *>(currVert - whence), this->theSingleBuffer, reinterpret_cast<const void *>(currCol - whence));
        //glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, SSBObindingPoint, this->theSingleBuffer, this->bufSize * this->currBuf, this->bufSize);
        glDrawArrays(GL_POINTS, 0, vertsThisTime);
        //glDrawArraysInstanced(GL_POINTS, 0, 1, vertsThisTime);
        this->lockSingle(fences[currBuf]);

        this->currBuf = (this->currBuf + 1) % this->numBuffers;
        vertCounter += vertsThisTime;
        currVert += vertsThisTime * 3 * sizeof(float);
    }

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glDisable(GL_TEXTURE_1D);
    newShader->Disable();
    glDisable(GL_VERTEX_PROGRAM_POINT_SIZE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void NVGDiagramRenderer::clusterYRange(float *const data, floattable::CallFloatTableData::ColumnInfo &ci, size_t selector, size_t numEntries, size_t numCol) {
    std::map<float, size_t> histogram;
    std::vector<float> means;
    std::vector<size_t> counters;
    std::vector<float> localmins;
    std::vector<float> localmaxes;
    std::vector<float> localranges;

    float clusterspace = 200;
    float tolerance = 10000;

    for (size_t i = 0; i < numEntries; i++) {
        float addr = data[selector + i*numCol];
        auto it = histogram.find(addr);
        if (it != histogram.end()) {
            it->second += 1;
        } else {
            histogram.insert(std::make_pair(addr, 1));
        }
    }

    for (auto &bin : histogram) {
        bool found = false;
        for (size_t clusterIdx = 0; clusterIdx < means.size(); clusterIdx++) {
            if (std::abs(means[clusterIdx] - bin.first) < tolerance) {
                size_t count = bin.second;
                size_t oldcount = counters[clusterIdx];
                means[clusterIdx] = means[clusterIdx] + ((bin.first - means[clusterIdx])*count) / (oldcount + count);
                if (bin.first < localmins[clusterIdx]) {
                    localmins[clusterIdx] = bin.first;
                }
                if (bin.first > localmaxes[clusterIdx]) {
                    localmaxes[clusterIdx] = bin.first;
                }
                found = true;
                break;
            }
        }
        if (!found) {
            means.push_back(bin.first);
            counters.push_back(bin.second);
            localmins.push_back(bin.first);
            localmaxes.push_back(bin.second);
        }
    }

    float totaladrs = 0.0f;

    for (size_t clusterIdx = 0; clusterIdx < means.size(); clusterIdx++) {
        float adrs = localmaxes[clusterIdx] - localmins[clusterIdx] + 1;
        localranges.push_back(adrs);
        totaladrs += adrs;
    }

    float totalheight = means.size() * clusterspace + totaladrs;
    float heightscale = numEntries / totalheight;

    std::vector<std::vector<float>> arrays(means.size());
    std::vector<std::vector<size_t>> indexarrays(means.size());

    for (size_t i = 0; i < numEntries; i++) {
        bool found = false;
        size_t clusterIdx = 0;
        for (clusterIdx = 0; clusterIdx < means.size(); clusterIdx++) {
            if (data[selector + i*numCol] >= localmins[clusterIdx] && data[selector + i*numCol] <= localmaxes[clusterIdx]) {
                found = true;
                break;
            }
        }
        if (found) {
            arrays[clusterIdx].push_back(data[selector + i*numCol]);
            indexarrays[clusterIdx].push_back(i);
        }
    }

    float clusterOffset = numEntries / (means.size());
    float clusterHeight = 0.9 * clusterOffset;

    float globalMin = (std::numeric_limits<float>::max)();
    float globalMax = (std::numeric_limits<float>::min)();

    float currpos = 0.0f;
    for (size_t clusterIdx = 0; clusterIdx < means.size(); clusterIdx++) {
        for (size_t index = 0; index < arrays[clusterIdx].size(); index++) {
            float val = arrays[clusterIdx][index];
            size_t idx = indexarrays[clusterIdx][index];

            float localpos = (val - localmins[clusterIdx]);
            float pos = localpos + currpos;
            pos *= heightscale;
            if (pos < globalMin) {
                globalMin = pos;
            }
            if (pos > globalMax) {
                globalMax = pos;
            }
            data[selector + idx*numCol] = pos;
        }
        currpos += localranges[clusterIdx] + clusterspace;
    }

    ci.SetMinimumValue(globalMin);
    ci.SetMaximumValue(globalMax);
}

void NVGDiagramRenderer::showToolTip(const float x, const float y,
    const std::string & symbol, const std::string & module, const std::string & file,
    const size_t & line, const size_t & memaddr, const size_t & memsize) const {
    auto ctx = static_cast<NVGcontext *>(this->nvgCtxt);
    float ttOH = 10;
    float ttOW = 10;
    float ttW = 200;
    float ttH = ttOH + 6 * (ttOH+BND_WIDGET_HEIGHT);

    float heightOffset = ttOH + BND_WIDGET_HEIGHT;

    nvgFontSize(ctx, 10.0f);
    nvgFillColor(ctx, nvgRGB(255, 255, 255));

    bndTooltipBackground(ctx, x, y, ttW, ttH);
    //auto text = std::string("Symbol: ") + symbol;
    auto text = symbol;
    bndTextField(ctx, x + ttOW, y + ttOH, ttW - 2 * ttOW, ttH, BND_CORNER_ALL, BND_DEFAULT, -1, text.c_str(), 0, text.size() - 1);
    /*text = std::string("Module: ") + module;
    bndTextField(ctx, x + ttOW, y + ttOH + 1 * heightOffset, ttW - 2 * ttOW, BND_WIDGET_HEIGHT, BND_CORNER_ALL, BND_DEFAULT, -1, text.c_str(), 0, text.size() - 1);
    text = std::string("Mem: ") + std::to_string(memaddr) + std::string(" ") + std::to_string(memsize);
    bndTextField(ctx, x + ttOW, y + ttOH + 2 * heightOffset, ttW - 2 * ttOW, BND_WIDGET_HEIGHT, BND_CORNER_ALL, BND_DEFAULT, -1, text.c_str(), 0, text.size() - 1);*/
}

size_t NVGDiagramRenderer::searchAndDispPointAttr(const float x, const float y) {
    // transform mouse coord x/y into OpenGL coordinate system
    auto test = vislib::math::Vector<float, 3>(x, y, 1.0f);
    auto ssp = this->transform*test;
    // look up coresponding point
    float rad = this->lineWidthParam.Param<core::param::FloatParam>()->Value();
    //rad /= this->transform.GetAt(0, 0);
    auto it = std::find_if(this->pointDataPoints.begin(), this->pointDataPoints.end(),
        [&rad, &ssp](const vislib::math::Point<float, 3> &a) {return vislib::math::Point<float, 2>(a.GetX(), a.GetY()).Distance(vislib::math::Point<float, 2>(ssp.X(), ssp.Y())) < rad;});
    auto idx = *reinterpret_cast<const int *>(&it->GetZ());

    TraceInfoCall *tic = this->metaRequestSlot.CallAs<megamol::infovis::TraceInfoCall>();
    if (tic == nullptr) {
        // show tool tip
        this->showToolTip(ssp.GetX() + 10, ssp.GetY() + 10,
            std::string("symbol"), std::string("module"), std::string("file"), size_t(1), size_t(it->X()), size_t(it->Y()));
    } else {
        tic->SetRequest(TraceInfoCall::RequestType::GetSymbolString, idx);
        if (!(*tic)(0)) {
            // show tool tip
            this->showToolTip(ssp.GetX() + 10, ssp.GetY() + 10,
                std::string("symbol"), std::string("module"), std::string("file"), size_t(1), size_t(it->X()), size_t(it->Y()));
        } else {
            auto st = tic->GetInfo();
            this->showToolTip(ssp.GetX() + 10, ssp.GetY() + 10,
                st, std::string("module"), std::string("file"), size_t(1), size_t(it->X()), size_t(it->Y()));
        }
    }

    return std::distance(this->pointDataPoints.begin(), it);
}

bool NVGDiagramRenderer::CalcExtents() {

    // TODO dirty checking and shit

    floattable::CallFloatTableData *ft = this->dataCallerSlot.CallAs<floattable::CallFloatTableData>();
    if (ft == nullptr) return false;

    if (!(*ft)(1)) return false;
    if (!(*ft)(0)) return false;

    if (!assertData(ft)) return false;

    //protein_calls::DiagramCall *diagram = this->dataCallerSlot.CallAs<protein_calls::DiagramCall>();
    //if (diagram == NULL) return false;
    //// execute the call
    //if (!(*diagram)(protein_calls::DiagramCall::CallForGetData)) return false;

    int type = this->diagramTypeParam.Param<param::EnumParam>()->Value();

    // TODO adjust
    bool autoFit = true;
    this->xRange.SetFirst(FLT_MAX);
    this->xRange.SetSecond(-FLT_MAX);
    this->yRange.SetFirst(FLT_MAX);
    this->yRange.SetSecond(-FLT_MAX);
    bool drawCategorical = this->drawCategoricalParam.Param<param::EnumParam>()->Value() != 0;
    if (autoFit) {
        const floattable::CallFloatTableData::ColumnInfo *const ci = &(ft->GetColumnsInfos()[this->abcissaIdx]);
        this->xRange.SetFirst(ci->MinimumValue());
        this->xRange.SetSecond(ci->MaximumValue());

        float *const data = const_cast<float *>(ft->GetData()); //< this is really toxic and evil

        for (int s = 0; s < this->columnSelectors.size(); s++) {
            //protein_calls::DiagramCall::DiagramSeries *ds = diagram->GetSeries(s);
            //const protein_calls::DiagramCall::DiagramMappable *dm = ds->GetMappable();

            size_t selector = std::get<1>(this->columnSelectors[s]);

            floattable::CallFloatTableData::ColumnInfo ci = (ft->GetColumnsInfos()[selector]);

            if (seriesVisible[s] /*&& isCategoricalMappable(dm) == drawCategorical*/) {
                /*vislib::Pair<float, float> xR = dm->GetAbscissaRange(0);
                vislib::Pair<float, float> yR = dm->GetOrdinateRange();*/

                //this->clusterYRange(data, ci, selector, ft->GetRowsCount(), ft->GetColumnsCount());

                vislib::Pair<float, float> yR = vislib::Pair<float, float>(ci.MinimumValue(), ci.MaximumValue());

                /*if (xR.First() < this->xRange.First()) {
                    this->xRange.SetFirst(xR.First());
                }
                if (xR.Second() > this->xRange.Second()) {
                    this->xRange.SetSecond(xR.Second());
                }*/
                if (yR.First() < this->yRange.First()) {
                    this->yRange.SetFirst(yR.First());
                }
                if (yR.Second() > this->yRange.Second()) {
                    this->yRange.SetSecond(yR.Second());
                }
            }
        }

        switch (type) {
        case DIAGRAM_TYPE_COLUMN_STACKED:
        case DIAGRAM_TYPE_LINE_STACKED:
        case DIAGRAM_TYPE_COLUMN_STACKED_NORMALIZED:
        case DIAGRAM_TYPE_LINE_STACKED_NORMALIZED:
            this->yRange.SetFirst(0.0f);
            this->yRange.SetSecond(1.0f);
            break;
        }
    }
    return true;
}

bool NVGDiagramRenderer::GetExtents(view::CallRender2D& call) {
    // set the bounding box to 0..1
    /*call.SetBoundingBox(0.0f - legendOffset - legendWidth, 0.0f - 2.0f * fontSize,
        this->aspectRatioParam.Param<param::FloatParam>()->Value() + fontSize, 1.0f + fontSize);*/
    call.SetBoundingBox(0.0f, 0.0f, call.GetWidth(), call.GetHeight());

    //this->CalcExtents();
    ////  ( this->yRange.Second() - this->yRange.First())
    ////            * this->aspectRatioParam.Param<param::FloatParam>()->Value() + this->xRange.First()
    //call.SetBoundingBox(xRange.First(), yRange.First(), xRange.Second(), yRange.Second());
    return true;
}

bool NVGDiagramRenderer::LoadIcon(vislib::StringA filename, int ID) {
    static vislib::graphics::BitmapImage img;
    static sg::graphics::PngBitmapCodec pbc;
    pbc.Image() = &img;
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    void *buf = NULL;
    SIZE_T size = 0;

    //if (pbc.Load(filename)) {
    if ((size = megamol::core::utility::ResourceWrapper::LoadResource(
        this->GetCoreInstance()->Configuration(), filename, &buf)) > 0) {
        if (pbc.Load(buf, size)) {
            img.Convert(vislib::graphics::BitmapImage::TemplateByteRGBA);
            for (unsigned int i = 0; i < img.Width() * img.Height(); i++) {
                BYTE r = img.PeekDataAs<BYTE>()[i * 4 + 0];
                BYTE g = img.PeekDataAs<BYTE>()[i * 4 + 1];
                BYTE b = img.PeekDataAs<BYTE>()[i * 4 + 2];
                if (r + g + b > 0) {
                    img.PeekDataAs<BYTE>()[i * 4 + 3] = 255;
                } else {
                    img.PeekDataAs<BYTE>()[i * 4 + 3] = 0;
                }
            }
            markerTextures.Add(vislib::Pair<int, vislib::SmartPtr<vislib::graphics::gl::OpenGLTexture2D> >());
            markerTextures.Last().First() = ID;
            markerTextures.Last().SetSecond(new vislib::graphics::gl::OpenGLTexture2D());
            if (markerTextures.Last().Second()->Create(img.Width(), img.Height(), false, img.PeekDataAs<BYTE>(), GL_RGBA) != GL_NO_ERROR) {
                Log::DefaultLog.WriteError("could not load %s texture.", filename.PeekBuffer());
                ARY_SAFE_DELETE(buf);
                return false;
            }
            markerTextures.Last().Second()->SetFilter(GL_LINEAR, GL_LINEAR);
            ARY_SAFE_DELETE(buf);
            return true;
        } else {
            Log::DefaultLog.WriteError("could not read %s texture.", filename.PeekBuffer());
        }
    } else {
        Log::DefaultLog.WriteError("could not find %s texture.", filename.PeekBuffer());
    }
    return false;
}


/*
 * Callback for mouse events (move, press, and release)
 */
bool NVGDiagramRenderer::MouseEvent(float x, float y, view::MouseFlags flags) {
    this->mouseX = x;
    this->mouseY = y;

    if (flags & view::MOUSEFLAG_BUTTON_LEFT_DOWN) {
        auto blubb = vislib::math::Vector<float, 3>(x, y, 1.0f);
        auto test = this->transform*blubb;

        printf("MouseCoord: %f, %f\n", test.GetX(), test.GetY());
        int i = 0;
        for (auto &r : this->bndBtns) {
            if (r.Contains(vislib::math::Point<float, 2>(test.GetX(), test.GetY()), true)) {
                this->selected[i] = !this->selected[i];
                return true;
            }
            i++;
        }
    } else if ((flags & view::MOUSEFLAG_BUTTON_RIGHT_CHANGED)
        && this->diagramTypeParam.Param<core::param::EnumParam>()->Value() == DiagramTypes::DIAGRAM_TYPE_POINT_SPLATS) {
        if (flags & view::MOUSEFLAG_BUTTON_RIGHT_DOWN) {
            // show tool tip for point
            this->mouseRightPressed = true;
            return true;
        } else {
            this->mouseRightPressed = false;
            return true;
        }
    }

    return false;

    //bool consumeEvent = false;

    //float aspect = this->aspectRatioParam.Param<param::FloatParam>()->Value();
    //float xObj = x / aspect;

    //if (flags & view::MOUSEFLAG_MODKEY_ALT_DOWN) {

    //    if (flags & view::MOUSEFLAG_BUTTON_LEFT_DOWN) {
    //        int type = this->diagramTypeParam.Param<param::EnumParam>()->Value();
    //        vislib::math::Point<float, 2> mouse(xObj, y);
    //        vislib::math::Point<float, 2> aspectlessMouse(x, y);
    //        vislib::math::Point<float, 2> pt3, pt4;
    //        vislib::math::ShallowPoint<float, 2> pt(mouse.PeekCoordinates());
    //        vislib::math::ShallowPoint<float, 2> pt2(mouse.PeekCoordinates());
    //        //printf("checking for hits\n");

    //        vislib::math::Rectangle<float> legend(-legendOffset - legendWidth,
    //            1.0f - legendHeight, -legendOffset, 1.0f);
    //        if (legend.Contains(aspectlessMouse)) {
    //            //printf("i am legend\n");
    //            if (diagram == NULL) {
    //                return false;
    //            }
    //            float series = 1.0f - legendMargin - mouse.Y();
    //            series /= theFont.LineHeight(fontSize);
    //            bool drawCategorical = this->drawCategoricalParam.Param<param::EnumParam>()->Value() != 0;
    //            vislib::Array<int> visibleSeries;
    //            visibleSeries.SetCapacityIncrement(10);
    //            for (int i = 0; i < (int)diagram->GetSeriesCount(); i++) {
    //                if (isCategoricalMappable(diagram->GetSeries(i)->GetMappable()) == drawCategorical) {
    //                    visibleSeries.Add(i);
    //                }
    //            }
    //            if (series < visibleSeries.Count()) {
    //                protein_calls::DiagramCall::DiagramSeries *ds = diagram->GetSeries(visibleSeries[static_cast<int>(series)]);
    //                float legendLeft = -legendOffset - legendWidth;
    //                if (legendLeft + legendMargin < x &&
    //                    x < legendLeft + legendMargin + 0.6 * fontSize) {
    //                    //printf("I think I hit the checkbox of series %s\n", ds->GetName());
    //                    //ds->SetVisible(!ds->GetVisible());
    //                    seriesVisible[static_cast<int>(series)] = !seriesVisible[static_cast<int>(series)];
    //                } else {
    //                    selectedSeries = ds;
    //                    //printf("I think I hit series %s\n", selectedSeries->GetName());
    //                    consumeEvent = true;
    //                }
    //            }
    //        } else {
    //            float dist = FLT_MAX;
    //            int distOffset = -1;
    //            if (type == DIAGRAM_TYPE_LINE || type == DIAGRAM_TYPE_LINE_STACKED
    //                || type == DIAGRAM_TYPE_LINE_STACKED_NORMALIZED) {

    //                for (int i = 0; i < (int)preparedData->Count(); i++) {
    //                    int leftNeighbor = -1;
    //                    int rightNeighbor = -1;
    //                    for (int j = 0; j < (int)(*preparedData)[i]->Count(); j++) {
    //                        if ((*(*preparedData)[i])[j] != NULL) {
    //                            if ((*(*preparedData)[i])[j]->GetX() > mouse.GetX()) {
    //                                break;
    //                            }
    //                            pt.SetPointer((*(*preparedData)[i])[j]->PeekCoordinates());
    //                            leftNeighbor = j;
    //                        }
    //                    }
    //                    for (int j = static_cast<int>((*preparedData)[i]->Count()) - 1; j > -1; j--) {
    //                        if ((*(*preparedData)[i])[j] != NULL) {
    //                            if ((*(*preparedData)[i])[j]->GetX() < mouse.GetX()) {
    //                                break;
    //                            }
    //                            pt2.SetPointer((*(*preparedData)[i])[j]->PeekCoordinates());
    //                            rightNeighbor = j;
    //                        }
    //                    }
    //                    if (leftNeighbor == -1 || rightNeighbor == -1
    //                        || (rightNeighbor - leftNeighbor) > 1) {
    //                        continue;
    //                    }
    //                    pt3 = pt.Interpolate(pt2, (mouse.GetX() - pt.X()) / (pt2.X() - pt.X()));
    //                    float d = pt3.Distance(mouse);
    //                    if (d < dist) {
    //                        dist = d;
    //                        distOffset = i;
    //                    }
    //                }
    //                if (distOffset != -1 && dist < 0.2f) {
    //                    //printf("I think I hit series %s[%u]\n", preparedSeries[distOffset]->GetName(), distOffset);
    //                    selectedSeries = preparedSeries[distOffset];
    //                    consumeEvent = true;
    //                } else {
    //                    selectedSeries = NULL;
    //                }
    //            } else if (type == DIAGRAM_TYPE_COLUMN || type == DIAGRAM_TYPE_COLUMN_STACKED
    //                || type == DIAGRAM_TYPE_COLUMN_STACKED_NORMALIZED) {

    //                for (int i = 0; i < (int)preparedData->Count(); i++) {
    //                    for (int j = 0; j < (int)(*preparedData)[i]->Count(); j++) {
    //                        if ((*(*preparedData)[i])[j] == NULL) {
    //                            continue;
    //                        }
    //                        float x2, y2;
    //                        getBarXY(i, j, type, &x2, &y2);
    //                        float y1 = (*(*preparedData)[i])[j]->GetZ();
    //                        float xDiff = xObj - x2;
    //                        if (xDiff > 0.0f && xDiff < barWidth && y > y1 && y < y2) {
    //                            //printf("I think I hit series %s[%u][%u]\n", preparedSeries[i]->GetName(), i, j);
    //                            selectedSeries = preparedSeries[i];
    //                            consumeEvent = true;
    //                            break;
    //                        }
    //                    }
    //                }
    //                if (!consumeEvent) {
    //                    selectedSeries = NULL;
    //                }
    //            }
    //        }
    //    } else {
    //    }
    //}

    //// propagate selection to selection module
    //if (selectionCall != NULL) {
    //    vislib::Array<int> selectedSeriesIndices;
    //    for (int x = 0; x < (int)this->diagram->GetSeriesCount(); x++) {
    //        if (this->diagram->GetSeries(x) == this->selectedSeries) {
    //            selectedSeriesIndices.Add(x);
    //            break;
    //        }
    //    }
    //    selectionCall->SetSelectionPointer(&selectedSeriesIndices);
    //    (*selectionCall)(protein_calls::IntSelectionCall::CallForSetSelection);
    //}

    //// propagate visibility to hidden module
    //if (hiddenCall != NULL) {
    //    vislib::Array<int> hiddenSeriesIndices;
    //    for (int x = 0; x < (int)this->diagram->GetSeriesCount(); x++) {
    //        if (!seriesVisible[x]) {
    //            hiddenSeriesIndices.Add(x);
    //        }
    //    }
    //    hiddenCall->SetSelectionPointer(&hiddenSeriesIndices);
    //    (*hiddenCall)(protein_calls::IntSelectionCall::CallForSetSelection);
    //}

    //// hovering
    //hoveredMarker = NULL;
    //if (preparedData != NULL) {
    //    for (int s = 0; s < (int)preparedData->Count(); s++) {
    //        float markerSize = fontSize;
    //        for (int i = 0; i < (int)preparedSeries[s]->GetMarkerCount(); i++) {
    //            const protein_calls::DiagramCall::DiagramMarker *m = preparedSeries[s]->GetMarker(i);
    //            for (int j = 0; j < (int)this->markerTextures.Count(); j++) {
    //                if (markerTextures[j].First() == m->GetType()) {
    //                    markerTextures[j].Second()->Bind();
    //                    // TODO FIXME BUG WTF does this happen anyway
    //                    if ((m->GetIndex() > (*preparedData)[s]->Count() - 1)
    //                        || (*(*preparedData)[s])[m->GetIndex()] == NULL) {
    //                        continue;
    //                    }
    //                    float mx = (*(*preparedData)[s])[m->GetIndex()]->X();
    //                    float my = (*(*preparedData)[s])[m->GetIndex()]->Y();
    //                    mx *= aspect;
    //                    mx -= markerSize / 2.0f;
    //                    my -= markerSize / 2.0f;
    //                    if (mx < x && x < mx + markerSize && my < y && y < my + markerSize) {
    //                        //printf("hovering over marker %u of series %s\n", i, preparedSeries[s]->GetName());
    //                        hoveredMarker = m;
    //                        hoveredSeries = s;
    //                    }
    //                }
    //            }
    //        }
    //    }
    //}
    //hoverPoint.Set(x, y);

    //return consumeEvent;
}

void NVGDiagramRenderer::seriesInsertionCB(const DiagramSeriesCall::DiagramSeriesTuple &tuple) {
    columnSelectors.push_back(tuple);
    //this->selected.push_back(true);
}

bool NVGDiagramRenderer::updateColumnSelectors(void) {
    DiagramSeriesCall *dsc = this->getSelectorsSlot.CallAs<DiagramSeriesCall>();
    if (dsc == nullptr) return false;

    std::vector<DiagramSeriesCall::DiagramSeriesTuple> oldColumnSelectors = this->columnSelectors;
    //auto oldSelected = this->selected;
    this->columnSelectors.clear();
    //this->selected.clear();

    dsc->SetSeriesInsertionCB(this->fpsicb);
    if (!(*dsc)(DiagramSeriesCall::CallForGetSeries)) {
        this->columnSelectors = oldColumnSelectors;
        //this->selected = oldSelected;
        return false;
    }
    this->selected.resize(this->columnSelectors.size(), true);

    return true;
}

bool NVGDiagramRenderer::isAnythingDirty() {
    return this->abcissaSelectorSlot.IsDirty() ||
        this->colSelectParam.IsDirty() ||
        this->descSelectParam.IsDirty();
}

void NVGDiagramRenderer::resetDirtyFlags() {
    this->abcissaSelectorSlot.ResetDirty();
    this->colSelectParam.ResetDirty();
    this->descSelectParam.ResetDirty();
}


/*
 * NVGDiagramRenderer::onCrosshairToggleButton
 */
bool NVGDiagramRenderer::onCrosshairToggleButton(param::ParamSlot& p) {
    param::BoolParam *bp = this->showCrosshairParam.Param<param::BoolParam>();
    bp->SetValue(!bp->Value());
    return true;
}


/*
 * NVGDiagramRenderer::onShowAllButton
 */
bool NVGDiagramRenderer::onShowAllButton(param::ParamSlot& p) {
    if (this->floatTable != nullptr) {
        for (int i = 0; i < this->columnSelectors.size(); i++) {
            //this->diagram->GetSeries(i)->SetVisible(true);
            seriesVisible[i] = true;
        }
    }
    return true;
}


/*
 * NVGDiagramRenderer::onHideAllButton
 */
bool NVGDiagramRenderer::onHideAllButton(param::ParamSlot& p) {
    if (this->floatTable != nullptr) {
        for (int i = 0; i < this->columnSelectors.size(); i++) {
            //this->diagram->GetSeries(i)->SetVisible(false);
            seriesVisible[i] = false;
        }
    }
    return true;
}


/*
 * Diagram2DRenderer::Render
 */
bool NVGDiagramRenderer::Render(view::CallRender2D &call) {
    floattable::CallFloatTableData *ft = this->dataCallerSlot.CallAs<floattable::CallFloatTableData>();
    if (ft == nullptr) return false;
    if (!(*ft)(1)) return false;
    if (!(*ft)(0)) return false;

    if (!assertData(ft)) return false;

    this->callR2D = &call;

    this->defineLayout(call.GetWidth(), call.GetHeight());

    float modelViewMatrix_column[16];
    float projMatrix_column[16];

 /*   glEnable(GL_POINT_SIZE);
    glPointSize(20.0f);
    glBegin(GL_POINTS);
    glVertex2f(0.f, 0.f);
    glVertex2f(call.GetWidth(), 0.f);
    glVertex2f(0.f, call.GetHeight());
    glVertex2f(call.GetWidth(), call.GetHeight());
    glEnd();*/

    // this is the apex of suck and must die
    glGetFloatv(GL_MODELVIEW_MATRIX, modelViewMatrix_column);
    glGetFloatv(GL_PROJECTION_MATRIX, projMatrix_column);
    // end suck
    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> t1;
    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> s;
    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> t2;
    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> t3;
    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> tr;
    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> sd;
    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> su;

    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> mv(modelViewMatrix_column[0], modelViewMatrix_column[4], modelViewMatrix_column[12],
        modelViewMatrix_column[1], modelViewMatrix_column[5], modelViewMatrix_column[13],
        modelViewMatrix_column[2], modelViewMatrix_column[6], 1.0f);
    vislib::math::Matrix<float, 3, vislib::math::COLUMN_MAJOR> p(projMatrix_column[0], projMatrix_column[4], projMatrix_column[12],
        projMatrix_column[1], projMatrix_column[5], projMatrix_column[13],
        projMatrix_column[2], projMatrix_column[6], 1.0f);

    auto mvp = p*mv;

    this->sWidth = call.GetWidth();
    this->sHeight = call.GetHeight();

    this->scaleX = modelViewMatrix_column[0];
    this->scaleY = modelViewMatrix_column[5];

    float xOff = static_cast<float>(this->screenSpaceMidPoint.GetX())/*+ modelViewMatrix_column[12] * this->scaleX*call.GetWidth()*/;
    float yOff = static_cast<float>(this->screenSpaceMidPoint.GetY())/*+ modelViewMatrix_column[13] * this->scaleY*call.GetHeight()*/;

    /*t1.SetAt(0, 2, -xOff);
    t1.SetAt(1, 2, -yOff);

    t2.SetAt(0, 2, xOff);
    t2.SetAt(1, 2, yOff);*/

    t1.SetAt(0, 2, 1.f);
    t1.SetAt(1, 2, 1.f);
    t1.SetAt(2, 2, 1.f);

    t2.SetAt(0, 2, 0.0f);
    t2.SetAt(1, 2, 1.0f);
    t2.SetAt(2, 2, 1.f);

    tr.SetAt(0, 2, modelViewMatrix_column[12] /* this->scaleX /* call.GetWidth()*/);
    tr.SetAt(1, 2, modelViewMatrix_column[13] /* this->scaleY /* call.GetHeight()*/);

    s.SetIdentity();
    s.SetAt(0, 0, 1.0f);
    s.SetAt(1, 1, -1.0f);

    t3.SetIdentity();
    t3.SetAt(0, 2, 0.0f);
    t3.SetAt(1, 2, -1.0f);

    su.SetAt(0, 0, 0.5f*call.GetWidth());
    su.SetAt(1, 1, 0.5f*call.GetHeight());

    sd.SetAt(0, 0, 1.0f/call.GetWidth());
    sd.SetAt(1, 1, 1.0f/call.GetHeight());

    //this->transform.SetIdentity();
    //this->transform = su*t2*p*mv*t1*sd;
    this->transform = su*t1*s*p*mv;
    this->transformT = su*t1*p*mv;

    /*this->transform.SetAt(0, 2, this->transform.GetAt(0, 2) + modelViewMatrix_column[12] *call.GetWidth());
    this->transform.SetAt(1, 2, this->transform.GetAt(1, 2) - modelViewMatrix_column[13] * call.GetHeight());*/

    // get pointer to Diagram2DCall
    //diagram = this->dataCallerSlot.CallAs<protein_calls::DiagramCall>();
    //if (diagram == NULL) return false;
    //// execute the call
    //if (!(*diagram)(protein_calls::DiagramCall::CallForGetData)) return false;

    /*selectionCall = this->selectionCallerSlot.CallAs<protein_calls::IntSelectionCall>();
    if (selectionCall != NULL) {
        (*selectionCall)(protein_calls::IntSelectionCall::CallForGetSelection);
        if (selectionCall->GetSelectionPointer() != NULL
            && selectionCall->GetSelectionPointer()->Count() > 0) {
            selectedSeries = diagram->GetSeries((*selectionCall->GetSelectionPointer())[0]);
        } else {
            selectedSeries = NULL;
        }
    }*/

    while (seriesVisible.Count() < this->columnSelectors.size()) {
        seriesVisible.Append(true);
    }

    // do we have visibility information propagated from outside?
    /*hiddenCall = this->hiddenCallerSlot.CallAs<protein_calls::IntSelectionCall>();
    if (hiddenCall != NULL) {
        (*hiddenCall)(protein_calls::IntSelectionCall::CallForGetSelection);
        if (hiddenCall->GetSelectionPointer() != NULL) {
            for (SIZE_T x = 0; x < seriesVisible.Count(); x++) {
                seriesVisible[x] = true;
            }
            if (hiddenCall->GetSelectionPointer()->Count() > 0) {
                vislib::Array<int> *sel = hiddenCall->GetSelectionPointer();
                for (SIZE_T x = 0; x < sel->Count(); x++) {
                    seriesVisible[(*sel)[x]] = false;
                }
            }
        }
    }*/

    if (this->foregroundColorParam.IsDirty()) {
        utility::ColourParser::FromString(
            this->foregroundColorParam.Param<param::StringParam>()->Value(),
            fgColor.PeekComponents()[0],
            fgColor.PeekComponents()[1],
            fgColor.PeekComponents()[2],
            fgColor.PeekComponents()[3]);
    }
    // TODO dirty checking and shit
    this->CalcExtents();

    ::glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // : GL_LINE);
    ::glDisable(GL_LINE_SMOOTH);
    ::glEnable(GL_DEPTH_TEST);
    //::glEnable(GL_LINE_WIDTH);
    //::glLineWidth(this->lineWidthParam.Param<param::FloatParam>()->Value());

    xAxis = 0.0f;
    yAxis = 0.0f;
    if (yRange.First() < 0.0f && yRange.Second() > 0.0f) {
        xAxis = -yRange.First() / (yRange.Second() - yRange.First());
    }
    if (xRange.First() < 0.0f && xRange.Second() > 0.0f) {
        yAxis = -xRange.First() / (xRange.Second() - xRange.First());
    }

    switch (this->diagramTypeParam.Param<param::EnumParam>()->Value()) {
    case DIAGRAM_TYPE_LINE:
    case DIAGRAM_TYPE_LINE_STACKED:
    case DIAGRAM_TYPE_LINE_STACKED_NORMALIZED:
        nvgBeginFrame(static_cast<NVGcontext *>(this->nvgCtxt), call.GetWidth(), call.GetHeight(), 1.0f);
        drawLineDiagram(call.GetWidth(), call.GetHeight());
        nvgEndFrame(static_cast<NVGcontext *>(this->nvgCtxt));
        break;
    case DIAGRAM_TYPE_COLUMN:
    case DIAGRAM_TYPE_COLUMN_STACKED:
    case DIAGRAM_TYPE_COLUMN_STACKED_NORMALIZED:
        nvgBeginFrame(static_cast<NVGcontext *>(this->nvgCtxt), call.GetWidth(), call.GetHeight(), 1.0f);
        drawColumnDiagram();
        nvgEndFrame(static_cast<NVGcontext *>(this->nvgCtxt));
        break;
    case DIAGRAM_TYPE_POINT_SPLATS:
        nvgBeginFrame(static_cast<NVGcontext *>(this->nvgCtxt), call.GetWidth(), call.GetHeight(), 1.0f);
        drawPointSplats(call.GetWidth(), call.GetHeight());
        nvgEndFrame(static_cast<NVGcontext *>(this->nvgCtxt));
        break;
    }

    /*if (this->mouseRightPressed) {
        searchAndDispPointAttr(this->mouseX, this->mouseY);
    }*/

    float aspect = this->aspectRatioParam.Param<param::FloatParam>()->Value();
    bool drawLog = this->drawYLogParam.Param<param::BoolParam>()->Value();
    if (this->showCrosshairParam.Param<param::BoolParam>()->Value()) {
        ::glDisable(GL_BLEND);
        ::glDisable(GL_DEPTH_TEST);
        vislib::StringA tmpString;
        float y;
        if (drawLog) {
            y = (float)pow(10, hoverPoint.GetY() * log10(yRange.Second() - yRange.First())) + yRange.First();
        } else {
            y = hoverPoint.GetY() * (yRange.Second() - yRange.First()) + yRange.First();
        }
        tmpString.Format("%f, %f", hoverPoint.GetX() / aspect * (xRange.Second() - xRange.First()) + xRange.First(), y);
        ::glBegin(GL_LINES);
        ::glColor4fv(this->fgColor.PeekComponents());
        ::glVertex3f(hoverPoint.GetX(), 0.0f, decorationDepth);
        ::glVertex3f(hoverPoint.GetX(), 1.0f, decorationDepth);
        ::glVertex3f(0.0f, hoverPoint.GetY(), decorationDepth);
        ::glVertex3f(aspect, hoverPoint.GetY(), decorationDepth);
        ::glEnd();
        theFont.DrawString(hoverPoint.GetX(), hoverPoint.GetY(), fontSize * 0.5f, true,
            tmpString.PeekBuffer(), vislib::graphics::AbstractFont::ALIGN_LEFT_BOTTOM);
    }

    /*if (this->showGuidesParam.Param<param::BoolParam>()->Value()) {
        for (int i = 0; i < (int)diagram->GetGuideCount(); i++) {
            protein_calls::DiagramCall::DiagramGuide *g = diagram->GetGuide(i);
            ::glDisable(GL_BLEND);
            ::glDisable(GL_DEPTH_TEST);
            vislib::StringA tmpString;
            tmpString.Format("%f", g->GetPosition());
            ::glEnable(GL_LINE_STIPPLE);
            ::glLineStipple(12, 0x5555);
            ::glBegin(GL_LINES);
            ::glColor4fv(this->fgColor.PeekComponents());
            float pos;
            switch (g->GetType()) {
            case protein_calls::DiagramCall::DIAGRAM_GUIDE_HORIZONTAL:
                pos = g->GetPosition() - yRange.First();
                pos /= yRange.GetSecond() - yRange.GetFirst();
                ::glVertex3f(0.0f, pos, decorationDepth);
                ::glVertex3f(aspect, pos, decorationDepth);
                ::glEnd();
                ::glDisable(GL_LINE_STIPPLE);
                theFont.DrawString(aspect, pos, fontSize * 0.5f, true,
                    tmpString.PeekBuffer(), vislib::graphics::AbstractFont::ALIGN_LEFT_BOTTOM);
                break;
            case protein_calls::DiagramCall::DIAGRAM_GUIDE_VERTICAL:
                pos = g->GetPosition() - xRange.First();
                pos /= xRange.GetSecond() - xRange.GetFirst();
                pos *= aspect;
                ::glVertex3f(pos, 0.0f, decorationDepth);
                ::glVertex3f(pos, 1.0f, decorationDepth);
                ::glEnd();
                ::glDisable(GL_LINE_STIPPLE);
                theFont.DrawString(pos, 1.0f, fontSize * 0.5f, true,
                    tmpString.PeekBuffer(), vislib::graphics::AbstractFont::ALIGN_LEFT_BOTTOM);
                break;
            }
        }
    }*/

    //if (hoveredMarker != NULL) {
    //    float x = (*(*preparedData)[hoveredSeries])[hoveredMarker->GetIndex()]->X();
    //    float y = (*(*preparedData)[hoveredSeries])[hoveredMarker->GetIndex()]->Y();
    //    x *= aspect;
    //    y += fontSize / 2.0f;
    //    theFont.DrawString(x, y, fontSize * 0.5f, true,
    //        hoveredMarker->GetTooltip(), vislib::graphics::AbstractFont::ALIGN_LEFT_BOTTOM);
    //    //"w3wt", vislib::graphics::AbstractFont::ALIGN_LEFT_BOTTOM);
    //}

    return true;
}


void NVGDiagramRenderer::drawYAxis() {
    int numYTicks = 0;
    float aspect = this->aspectRatioParam.Param<param::FloatParam>()->Value();
    vislib::StringA *yTickText = NULL;
    float *yTicks = NULL;
    if (this->drawYLogParam.Param<param::BoolParam>()->Value()) {
        int startExp, destExp;
        if (yRange.First() == 0.0f) {
            startExp = 0;
        } else {
            startExp = static_cast<int>(ceil(log10(yRange.First())));
        }
        if (yRange.Second() == 0.0f) {
            destExp = 0;
        } else {
            destExp = static_cast<int>(floor(log10(yRange.Second())));
        }
        if (startExp > destExp) {
            destExp = startExp;
        }
        // WARNING: the yRange extremes potentially overlap with [startExp;destExp]
        // making part of this array superfluous. If string drawing is not robust,
        // there might be a detonation
        numYTicks = static_cast<int>(destExp - startExp) + 1 + 2;
        yTickText = new vislib::StringA[numYTicks];
        yTicks = new float[numYTicks];
        yTicks[0] = 0.0f;
        yTickText[0].Format("%.2f", yRange.First());
        yTicks[numYTicks - 1] = 1.0f;
        yTickText[numYTicks - 1].Format("%.2f", yRange.Second());

        for (int i = startExp; i <= destExp; i++) {
            yTickText[i] = vislib::StringA::EMPTY;
            float yVal = (float)pow(10, static_cast<float>(i));
            yTickText[i].Format("%.2f", yVal);
            yTicks[i] = log10(yVal - yRange.First()) / log10(yRange.Second() - yRange.First());
        }
    } else {
        numYTicks = this->numYTicksParam.Param<param::IntParam>()->Value();
        yTickText = new vislib::StringA[numYTicks];
        yTicks = new float[numYTicks];
        float yTickLabel = (yRange.Second() - yRange.First()) / (numYTicks - 1);
        float yTickOff = 1.0f / (numYTicks - 1);
        for (int i = 0; i < numYTicks; i++) {
            yTickText[i] = vislib::StringA::EMPTY;
            yTickText[i].Format("%.2f", yTickLabel * i + yRange.First());
            yTicks[i] = (1.0f / (numYTicks - 1)) * i;
        }
    }

    float arrWidth = 0.025f;
    float arrHeight = 0.05f;
    float tickSize = fontSize;

    /*::glBegin(GL_LINES);
    ::glColor4fv(this->fgColor.PeekComponents());
    ::glVertex3f(yAxis, 0.0f, decorationDepth);
    ::glVertex3f(yAxis, 1.0f + 2.0f * arrWidth, decorationDepth);
    ::glVertex3f(yAxis, 1.0f + 2.0f * arrWidth, decorationDepth);
    ::glVertex3f(yAxis - arrHeight, 1.0f + 1.0f * arrWidth, decorationDepth);
    ::glVertex3f(yAxis, 1.0f + 2.0f * arrWidth, decorationDepth);
    ::glVertex3f(yAxis + arrHeight, 1.0f + 1.0f * arrWidth, decorationDepth);
    ::glEnd();

    for (int i = 0; i < numYTicks; i++) {
        ::glBegin(GL_LINES);
        ::glVertex3f(yAxis, yTicks[i], decorationDepth);
        ::glVertex3f(yAxis - tickSize * 0.5f, yTicks[i], decorationDepth);
        ::glEnd();
        theFont.DrawString(yAxis - tickSize * 0.5f, yTicks[i], fontSize, true, yTickText[i],
            vislib::graphics::AbstractFont::ALIGN_RIGHT_TOP);
    }*/


    float dw = this->screenSpaceDiagramSize.GetWidth();
    float dh = this->screenSpaceDiagramSize.GetHeight();
    float mw = this->screenSpaceMidPoint.GetX();
    float mh = this->screenSpaceMidPoint.GetY();

    NVGcontext *ctx = static_cast<NVGcontext *>(this->nvgCtxt);
    nvgSave(ctx);
    nvgTransform(ctx, this->transform.GetAt(0, 0), this->transform.GetAt(1, 0),
        this->transform.GetAt(0, 1), this->transform.GetAt(1, 1),
        this->transform.GetAt(0, 2), this->transform.GetAt(1, 2));

    nvgStrokeWidth(ctx, 2.0f);
    nvgStrokeColor(ctx, nvgRGB(255, 255, 255));

    nvgFontSize(ctx, fontSize*dh);
    nvgFontFace(ctx, "sans");
    nvgTextAlign(ctx, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(ctx, nvgRGB(255, 255, 255));
    //nvgTextLineHeight(ctx, 1.2f);


    nvgBeginPath(ctx);
    nvgMoveTo(ctx, mw - dw / 2, mh - dh / 2);
    nvgLineTo(ctx, mw - dw / 2, mh + (0.5f + arrHeight)*dh);
    nvgMoveTo(ctx, mw - (0.5f + arrWidth)*dw, mh + dh/2);
    nvgLineTo(ctx, mw - dw / 2, mh + (0.5f + arrHeight)*dh);
    nvgMoveTo(ctx, mw - (0.5f - arrWidth)*dw, mh + dh/2);
    nvgLineTo(ctx, mw - dw / 2, mh + (0.5f + arrHeight)*dh);
    nvgStroke(ctx);

    for (int i = 0; i < numYTicks; i++) {
        nvgBeginPath(ctx);
        nvgMoveTo(ctx, mw - dw / 2, mh - (0.5f-yTicks[i])*dh);
        nvgLineTo(ctx, mw - (0.5f + tickSize)*dw, mh - (0.5f-yTicks[i])*dh);
        nvgStroke(ctx);
        /*theFont.DrawString(yAxis - tickSize * 0.5f, yTicks[i], fontSize, true, yTickText[i],
            vislib::graphics::AbstractFont::ALIGN_RIGHT_TOP);*/
        //nvgText(ctx, mw - (0.5f + tickSize)*dw, mh - (0.5f - yTicks[i])*dh, yTickText[numYTicks - 1 - i], nullptr);
    }

    nvgScale(ctx, 1.0f, -1.0f);
    nvgTranslate(ctx, 0.0f, -this->sHeight);
    for (int i = 0; i < numYTicks; i++) {
        /*theFont.DrawString(yAxis - tickSize * 0.5f, yTicks[i], fontSize, true, yTickText[i],
        vislib::graphics::AbstractFont::ALIGN_RIGHT_TOP);*/
        nvgText(ctx, mw - (0.5f + tickSize)*dw, mh - (0.5f - yTicks[i])*dh, yTickText[numYTicks - 1 - i], nullptr);
    }

    nvgRestore(ctx);


    delete[] yTickText;
    delete[] yTicks;
}


void NVGDiagramRenderer::drawXAxis(XAxisTypes xType) {
    if (this->columnSelectors.size() == 0) {
        xTickOff = 0.0f;
    }
    int numXTicks;
    switch (xType) {
    case DIAGRAM_XAXIS_FLOAT:
        numXTicks = this->numXTicksParam.Param<param::IntParam>()->Value();
        break;
    case DIAGRAM_XAXIS_INTEGRAL:
    {
        //numXTicks = 0;
        //for (int i = 0; i < diagram->GetSeriesCount(); i++) {
        //    DiagramCall::DiagramSeries *ds = diagram->GetSeries(i);
        //    const DiagramCall::DiagramMappable *dm = ds->GetMappable();
        //    if (dm->GetDataCount() > numXTicks) {
        //        numXTicks = dm->GetDataCount();
        //    }
        //}
        //numXTicks++;
        numXTicks = (int)xValues.Count();
    }
    break;
    case DIAGRAM_XAXIS_CATEGORICAL:
        numXTicks = static_cast<int>(categories.Count() + 1);
        break;
    }
    vislib::StringA *xTickText = new vislib::StringA[numXTicks];
    float xTickLabel = (xRange.Second() - xRange.First()) / (numXTicks - 1);
    for (int i = 0; i < numXTicks; i++) {
        xTickText[i] = vislib::StringA::EMPTY;
        switch (xType) {
        case DIAGRAM_XAXIS_FLOAT:
            xTickText[i].Format("%.2f", xTickLabel * i + xRange.First());
            break;
        case DIAGRAM_XAXIS_INTEGRAL:
            xTickText[i].Format("%u", i);
            break;
        case DIAGRAM_XAXIS_CATEGORICAL:
            // not needed
            break;
        }
    }
    float aspect;
    if (this->autoAspectParam.Param<param::BoolParam>()->Value()) {
        switch (xType) {
        case DIAGRAM_XAXIS_FLOAT:
            // cannot think of anything better actually
            aspect = this->aspectRatioParam.Param<param::FloatParam>()->Value();
            break;
        case DIAGRAM_XAXIS_INTEGRAL:
            aspect = numXTicks * theFont.LineWidth(fontSize, xTickText[numXTicks - 1].PeekBuffer()) * 1.5f;
            break;
        case DIAGRAM_XAXIS_CATEGORICAL:
        {
            float wMax = 0.0f;
            for (int i = 0; i < (int)categories.Count(); i++) {
                float w = theFont.LineWidth(fontSize, categories[i].PeekBuffer());
                if (w > wMax) {
                    wMax = w;
                }
            }
            wMax *= 2.0f;
            aspect = wMax * categories.Count();
        }
        break;
        }
        this->aspectRatioParam.Param<param::FloatParam>()->SetValue(aspect);
    } else {
        aspect = this->aspectRatioParam.Param<param::FloatParam>()->Value();
    }
    float arrWidth = 0.05f;/* / aspect;*/
    float arrHeight = 0.025f;
    float tickSize = fontSize;


    float dw = this->screenSpaceDiagramSize.GetWidth();
    float dh = this->screenSpaceDiagramSize.GetHeight();
    float mw = this->screenSpaceMidPoint.GetX();
    float mh = this->screenSpaceMidPoint.GetY();

    NVGcontext *ctx = static_cast<NVGcontext *>(this->nvgCtxt);
    nvgSave(ctx);
    nvgTransform(ctx, this->transform.GetAt(0, 0), this->transform.GetAt(1, 0),
        this->transform.GetAt(0, 1), this->transform.GetAt(1, 1),
        this->transform.GetAt(0, 2), this->transform.GetAt(1, 2));

    nvgStrokeWidth(ctx, 2.0f);
    nvgStrokeColor(ctx, nvgRGB(255, 255, 255));

    nvgFontSize(ctx, fontSize*dh);
    nvgFontFace(ctx, "sans");
    nvgTextAlign(ctx, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgFillColor(ctx, nvgRGB(255, 255, 255));

    nvgBeginPath(ctx);
    nvgMoveTo(ctx, mw - dw / 2, mh - dh / 2);
    nvgLineTo(ctx, mw + (0.5f + arrWidth)*dw, mh - dh / 2);
    nvgMoveTo(ctx, mw + dw / 2, mh - (0.5f - arrHeight)*dh);
    nvgLineTo(ctx, mw + (0.5f + arrWidth)*dw, mh - dh / 2);
    nvgMoveTo(ctx, mw + dw / 2, mh - (0.5f + arrHeight)*dh);
    nvgLineTo(ctx, mw + (0.5f + arrWidth)*dw, mh - dh / 2);
    nvgStroke(ctx);

    xTickOff = 1.0f / (numXTicks - 1);
    for (int i = 0; i < numXTicks; i++) {
        nvgBeginPath(ctx);
        nvgMoveTo(ctx, mw + (xTickOff*i - 0.5f)*dw, mh - dh / 2);
        nvgLineTo(ctx, mw + (xTickOff*i - 0.5f)*dw, mh - (0.5f + tickSize)*dh);
        nvgStroke(ctx);
    }

    /*::glPushMatrix();
    ::glScalef(aspect, 1.0f, 1.0f);
    ::glBegin(GL_LINES);
    ::glColor4fv(this->fgColor.PeekComponents());
    ::glVertex3f(0.0f, xAxis, decorationDepth);
    ::glVertex3f(1.0f + 2.0f * arrWidth, xAxis, decorationDepth);
    ::glVertex3f(1.0f + 2.0f * arrWidth, xAxis, decorationDepth);
    ::glVertex3f(1.0f + 1.0f * arrWidth, xAxis - arrHeight, decorationDepth);
    ::glVertex3f(1.0f + 2.0f * arrWidth, xAxis, decorationDepth);
    ::glVertex3f(1.0f + 1.0f * arrWidth, xAxis + arrHeight, decorationDepth);
    ::glEnd();
    xTickOff = 1.0f / (numXTicks - 1);
    for (int i = 0; i < numXTicks; i++) {
        ::glBegin(GL_LINES);
        ::glVertex3f(xTickOff * i, xAxis, decorationDepth);
        ::glVertex3f(xTickOff * i, xAxis - tickSize * 0.5f, decorationDepth);
        ::glEnd();
    }
    ::glPopMatrix();*/

    /*nvgTransform(ctx, this->transformT.GetAt(0, 0), this->transformT.GetAt(1, 0),
        this->transformT.GetAt(0, 1), this->transformT.GetAt(1, 1),
        this->transformT.GetAt(0, 2), this->transformT.GetAt(1, 2));*/
    nvgScale(ctx, 1.0f, -1.0f);
    nvgTranslate(ctx, 0.0f, -this->sHeight);

    switch (xType) {
    case DIAGRAM_XAXIS_CATEGORICAL:
    {
        for (int i = 0; i < numXTicks - 1; i++) {
            /*theFont.DrawString(aspect * (xTickOff * (i + 0.5f)), xAxis - tickSize * 0.5f, fontSize, true, categories[i].PeekBuffer(),
                vislib::graphics::AbstractFont::ALIGN_CENTER_TOP);*/

            nvgText(ctx, mw + (xTickOff*i - 0.5f)*dw, mh + (0.5f + tickSize)*dh, categories[i].PeekBuffer(), nullptr);
        }
    }
    break;
    case DIAGRAM_XAXIS_INTEGRAL:
    {
        float needed = theFont.LineWidth(fontSize, xTickText[numXTicks - 1]);
        int step = vislib::math::Max(static_cast<int>(needed / (xTickOff / aspect)), 1);
        for (int i = 0; i < numXTicks - 1; i += step) {
            /*theFont.DrawString(aspect * (xTickOff * (i + 0.5f)), xAxis - tickSize * 0.5f, fontSize, true, xTickText[i],
                vislib::graphics::AbstractFont::ALIGN_CENTER_TOP);*/

            nvgText(ctx, mw + (xTickOff*i - 0.5f)*dw, mh + (0.5f + tickSize)*dh, xTickText[i], nullptr);
        }
    }
    break;
    case DIAGRAM_XAXIS_FLOAT:
    {
        for (int i = 0; i < numXTicks; i++) {
            /*theFont.DrawString(aspect * (xTickOff * i), xAxis - tickSize * 0.5f, fontSize, true, xTickText[i],
                vislib::graphics::AbstractFont::ALIGN_LEFT_TOP);*/

            nvgText(ctx, mw + (xTickOff*i - 0.5f)*dw, mh + (0.5f + tickSize)*dh, xTickText[i], nullptr);
        }
    }
    break;
    }

    nvgRestore(ctx);

    delete[] xTickText;
}

void NVGDiagramRenderer::drawLegend(float w, float h) {
    float dw = this->screenSpaceDiagramSize.GetWidth();
    float dh = this->screenSpaceDiagramSize.GetHeight();
    float sw = this->screenSpaceCanvasSize.GetWidth();
    float sh = this->screenSpaceCanvasSize.GetHeight();
    float mw = this->screenSpaceMidPoint.GetX();
    float mh = this->screenSpaceMidPoint.GetY();

    float lw = 200;

    float low = 20;
    float loh = 20;

    float lh = loh + (loh + BND_WIDGET_HEIGHT)*(this->columnSelectors.size());

    NVGcontext *ctx = static_cast<NVGcontext *>(this->nvgCtxt);
    nvgSave(ctx);
    nvgFontSize(ctx, fontSize*dh);
    nvgFillColor(ctx, nvgRGB(128, 128, 128));
    nvgBeginPath(ctx);
    nvgRoundedRect(ctx, w - lw - low, loh, lw, lh, 3);
    nvgFill(ctx);

    this->bndBtns.clear();

    int i = 0;
    for (auto &s : this->columnSelectors) {
        auto color = std::get<4>(s);

        BNDwidgetState state = BND_ACTIVE;
        if (!this->selected[i]) {
            state = BND_DEFAULT;
        }

        this->bndBtns.push_back(vislib::math::Rectangle<float>(w - lw + BND_WIDGET_HEIGHT + 10,
            (2 * loh + (loh + BND_WIDGET_HEIGHT)*(i)),
            w - lw + BND_WIDGET_HEIGHT + 10 + 100,
            (2 * loh + (loh + BND_WIDGET_HEIGHT)*(i)+BND_WIDGET_HEIGHT)
        ));

        bndColorButton(ctx, w - lw, 2 * loh + (loh + BND_WIDGET_HEIGHT)*(i), BND_WIDGET_HEIGHT, BND_WIDGET_HEIGHT, BND_CORNER_ALL, nvgRGBf(color[0], color[1], color[2]));
        bndOptionButton(ctx, w - lw + BND_WIDGET_HEIGHT + 10, 2 * loh + (loh + BND_WIDGET_HEIGHT)*(i), 100, BND_WIDGET_HEIGHT, state, std::get<2>(s).c_str());
        i++;
    }

    nvgRestore(ctx);

    //legendWidth = 0.0f;
    //vislib::StringA s;
    //s.Format("%.2f", yRange.Second());
    //legendOffset = theFont.LineWidth(fontSize, s) + fontSize; //3.0f * fontSize;
    //bool drawCategorical = this->drawCategoricalParam.Param<param::EnumParam>()->Value() != 0;
    //int cnt = 0;
    //for (int s = 0; s < this->columnSelectors.size(); s++) {
    //    protein_calls::DiagramCall::DiagramSeries *ds = diagram->GetSeries(s);
    //    if (isCategoricalMappable(ds->GetMappable()) == drawCategorical) {
    //        float w = theFont.LineWidth(fontSize, ds->GetName());
    //        if (w > legendWidth) {
    //            legendWidth = w;
    //        }
    //        cnt++;
    //    }
    //}
    //legendMargin = legendWidth * 0.1f;
    //legendHeight = theFont.LineHeight(fontSize) * cnt + 2.0f * legendMargin;
    //legendWidth = legendWidth + 2.0f * legendMargin + fontSize * 0.8f;
    //float legendLeft = -legendOffset - legendWidth;
    //::glBegin(GL_LINE_STRIP);
    //::glColor4fv(this->fgColor.PeekComponents());
    //::glVertex3f(-legendOffset, 1.0f, decorationDepth);
    //::glVertex3f(legendLeft, 1.0f, decorationDepth);
    //::glVertex3f(legendLeft, 1.0f - legendHeight, decorationDepth);
    //::glVertex3f(-legendOffset, 1.0f - legendHeight, decorationDepth);
    //::glVertex3f(-legendOffset, 1.0f, decorationDepth);
    //::glEnd();
    //cnt = 0;
    //for (int s = 0; s < (int)diagram->GetSeriesCount(); s++) {
    //    protein_calls::DiagramCall::DiagramSeries *ds = diagram->GetSeries(s);
    //    if (isCategoricalMappable(ds->GetMappable()) == drawCategorical) {
    //        if (selectedSeries == NULL || *selectedSeries == *ds) {
    //            ::glColor4fv(ds->GetColor().PeekComponents());
    //        } else {
    //            ::glColor4fv(unselectedColor.PeekComponents());
    //        }
    //        float y = 1.0f - legendMargin - theFont.LineHeight(fontSize) * cnt;
    //        theFont.DrawString(-legendOffset - legendMargin, y,
    //            fontSize, true, ds->GetName(), vislib::graphics::AbstractFont::ALIGN_RIGHT_TOP);
    //        ::glBegin(GL_LINE_STRIP);
    //        ::glVertex3f(legendLeft + legendMargin, y - 0.2f * fontSize, decorationDepth);
    //        ::glVertex3f(legendLeft + legendMargin, y - 0.8f * fontSize, decorationDepth);
    //        ::glVertex3f(legendLeft + legendMargin + 0.6f * fontSize, y - 0.8f * fontSize, decorationDepth);
    //        ::glVertex3f(legendLeft + legendMargin + 0.6f * fontSize, y - 0.2f * fontSize, decorationDepth);
    //        ::glVertex3f(legendLeft + legendMargin, y - 0.2f * fontSize, decorationDepth);
    //        ::glEnd();
    //        if (seriesVisible[s]) {
    //            ::glBegin(GL_LINES);
    //            ::glVertex3f(legendLeft + legendMargin, y - 0.2f * fontSize, decorationDepth);
    //            ::glVertex3f(legendLeft + legendMargin + 0.6f * fontSize, y - 0.8f * fontSize, decorationDepth);
    //            ::glVertex3f(legendLeft + legendMargin + 0.6f * fontSize, y - 0.2f * fontSize, decorationDepth);
    //            ::glVertex3f(legendLeft + legendMargin, y - 0.8f * fontSize, decorationDepth);
    //            ::glEnd();
    //        }
    //        cnt++;
    //    }
    //}
}

void NVGDiagramRenderer::getBarXY(int series, int index, int type, float *x, float *y) {
    /*if (isCategoricalMappable(preparedSeries[series]->GetMappable())) {
        *x = (*(*preparedData)[series])[index]->GetX();
    } else {
        *x = static_cast<float>(index);
    }*/
    *x = static_cast<float>(index);

    if (type == DIAGRAM_TYPE_COLUMN_STACKED || type == DIAGRAM_TYPE_COLUMN_STACKED_NORMALIZED) {
        *x = yAxis + (*x + 0.5f) * xTickOff - xTickOff * 0.5f * barWidthRatio;
    } else {
        *x = yAxis + (*x + 0.5f) * xTickOff - xTickOff * 0.5f * barWidthRatio + series * barWidth;
    }
    *y = (*(*preparedData)[series])[index]->GetY();
}

int floatComp(const float& lhs, const float& rhs) {
    if (lhs < rhs) {
        return -1;
    } else if (lhs > rhs) {
        return 1;
    } else {
        return 0;
    }
}

void NVGDiagramRenderer::prepareData(bool stack, bool normalize, bool drawCategorical) {
    if (preparedData != NULL) {
        // TODO: why??
        //preparedData->Clear();
        delete preparedData;
        preparedData = NULL;
    }
    preparedData = new vislib::PtrArray<vislib::PtrArray<vislib::math::Point<float, 3> > >();
    vislib::Array<float> maxYValues;
    xValues.Clear();
    xValues.SetCapacityIncrement(10);
    bool drawLog = this->drawYLogParam.Param<param::BoolParam>()->Value();
    categories.Clear();
    preparedSeries.Clear();
    vislib::StringA tmpString;
    int maxCount = 0;
    float maxStackedY = -FLT_MAX;
    float x, y, z, tempX;
    // find "broadest" series as well as all distinct abscissa values (for stacking)

    floattable::CallFloatTableData *ft = this->floatTable; // TODO: Set this!!!
    const float *const data = ft->GetData();
    //vislib::sys::Log::DefaultLog.WriteInfo("NVGDiagramRenderer: Abcissa is %d\n", this->abcissaIdx);

    //for (int s = 0; s < this->columnSelectors.size(); s++) {
    //    size_t selector = std::get<1>(this->columnSelectors[s]);

    //    const floattable::CallFloatTableData::ColumnInfo *const ci = &(ft->GetColumnsInfos()[selector]);

    //    //protein_calls::DiagramCall::DiagramSeries *ds = diagram->GetSeries(s);
    //    //const protein_calls::DiagramCall::DiagramMappable *dm = ds->GetMappable();

    //    if (ft->GetRowsCount() > maxCount) {
    //        maxCount = ft->GetRowsCount();
    //    }
    //    if (!seriesVisible[s] /*|| isCategoricalMappable(dm) != drawCategorical*/) {
    //        continue;
    //    }
    //    for (int i = 0; i < ft->GetRowsCount(); i++) {
    //        //if (drawCategorical) {
    //        //    bool ret = dm->GetAbscissaValue(i, 0, &tmpString);
    //        //    if (ret) {
    //        //        int idx = static_cast<int>(categories.IndexOf(tmpString));
    //        //        if (idx == vislib::Array<vislib::StringA>::INVALID_POS) {
    //        //            categories.Add(tmpString);
    //        //            //idx = static_cast<int>(categories.Count() - 1);
    //        //            xValues.Add(static_cast<float>(idx));
    //        //        }
    //        //    }
    //        //} else {
    //        //    bool ret = dm->GetAbscissaValue(i, 0, &x);
    //        //    if (ret) {
    //        //        if (!xValues.Contains(x)) {
    //        //            xValues.Add(x);
    //        //        }
    //        //    }
    //        //}

    //        if (!xValues.Contains(data[this->abcissaIdx + i*ft->GetColumnsCount()])) {
    //            xValues.Add(data[this->abcissaIdx + i*ft->GetColumnsCount()]);
    //        }
    //    }
    //}

    for (int i = 0; i < ft->GetRowsCount(); i++) {
        //if (drawCategorical) {
        //    bool ret = dm->GetAbscissaValue(i, 0, &tmpString);
        //    if (ret) {
        //        int idx = static_cast<int>(categories.IndexOf(tmpString));
        //        if (idx == vislib::Array<vislib::StringA>::INVALID_POS) {
        //            categories.Add(tmpString);
        //            //idx = static_cast<int>(categories.Count() - 1);
        //            xValues.Add(static_cast<float>(idx));
        //        }
        //    }
        //} else {
        //    bool ret = dm->GetAbscissaValue(i, 0, &x);
        //    if (ret) {
        //        if (!xValues.Contains(x)) {
        //            xValues.Add(x);
        //        }
        //    }
        //}

        xValues.Add(data[this->abcissaIdx + i*ft->GetColumnsCount()]);
    }

    //xValues.Sort(&floatComp);
    maxYValues.SetCount(xValues.Count());
    for (int i = 0; i < (int)maxYValues.Count(); i++) {
        maxYValues[i] = 0.0f;
    }
    // there is a difference between not finding an x value and having a hole which is explicitly returned as NULL
    localXIndexToGlobal.SetCount(this->columnSelectors.size());

#if 1
    int cntSeries = 0;
    this->pointData.clear();
    this->pointData.reserve(this->columnSelectors.size()*ft->GetRowsCount() * 2);
    for (int s = 0; s < this->columnSelectors.size(); s++) {
        //protein_calls::DiagramCall::DiagramSeries *ds = diagram->GetSeries(s);
        //const protein_calls::DiagramCall::DiagramMappable *dm = ds->GetMappable();

        size_t selector = std::get<1>(this->columnSelectors[s]);

        const floattable::CallFloatTableData::ColumnInfo *const ci = &(ft->GetColumnsInfos()[selector]);

        //if (!seriesVisible[s] /*|| isCategoricalMappable(dm) != drawCategorical*/) {
        //if (!this->selected[s] /*|| isCategoricalMappable(dm) != drawCategorical*/) {
        //    continue;
        //}
        cntSeries++;
        localXIndexToGlobal[cntSeries - 1].SetCount(ft->GetRowsCount());
        if ((int)preparedData->Count() < cntSeries) {
            preparedData->Append(new vislib::PtrArray<vislib::math::Point<float, 3> >());
            preparedSeries.Append(ci);
            (*preparedData)[preparedData->Count() - 1]->SetCount(xValues.Count());
        }
        int globalX = 0;
        bool inHole = true, ret;

        size_t colIdx = std::get<1>(this->columnSelectors[s]);
        /*size_t descIdx = 0;
        for (size_t i = 0; i < ft->GetColumnsCount(); i++) {
            if (ft->GetColumnsInfos()[i].Name().compare("desc") == 0) {
                descIdx = i;
                break;
            }
        }*/

        for (int localX = 0; localX < ft->GetRowsCount(); localX++) {
            /*if (drawCategorical) {
                ret = dm->GetAbscissaValue(localX, 0, &tmpString);
                if (ret) {
                    int idx = static_cast<int>(categories.IndexOf(tmpString));
                    tempX = static_cast<float>(idx);
                    while (xValues[globalX] < tempX) {
                        if (inHole) {
                            (*(*preparedData)[cntSeries - 1])[globalX] = NULL;
                        } else {
                            (*(*preparedData)[cntSeries - 1])[globalX] = new vislib::math::Point<float, 3>(x, y, z);
                        }
                        globalX++;
                    }
                    ASSERT(xValues[globalX] == tempX);
                    localXIndexToGlobal[cntSeries - 1][localX] = globalX;
                    y = dm->GetOrdinateValue(localX);
                }
            } else {
                ret = dm->GetAbscissaValue(localX, 0, &tempX);
                if (ret) {
                    while (xValues[globalX] < tempX) {
                        if (inHole) {
                            (*(*preparedData)[cntSeries - 1])[globalX] = NULL;
                        } else {
                            (*(*preparedData)[cntSeries - 1])[globalX] = new vislib::math::Point<float, 3>(x, y, z);
                        }
                        globalX++;
                    }
                    ASSERT(xValues[globalX] == tempX);
                    localXIndexToGlobal[cntSeries - 1][localX] = globalX;
                    x = tempX - xRange.First();
                    x /= xRange.Second() - xRange.First();
                    y = dm->GetOrdinateValue(localX);
                }
            }
            if (ret) {
                z = 0.0f;
                (*(*preparedData)[cntSeries - 1])[globalX] = new vislib::math::Point<float, 3>(x, y, z);
            }
            inHole = !ret;*/
            x = xValues[localX];
            y = data[colIdx + localX*ft->GetColumnsCount()];
            z = data[this->descColIdx + localX*ft->GetColumnsCount()];
            (*(*preparedData)[cntSeries - 1])[localX] = new vislib::math::Point<float, 3>(x, y, z);

            /*this->pointData.push_back(this->screenSpaceMidPoint.GetX() + x*this->screenSpaceDiagramSize.GetWidth());
            this->pointData.push_back(this->screenSpaceMidPoint.GetY() + y*this->screenSpaceDiagramSize.GetHeight());*/
            
        }
    }
#else // old, wrong implementation
    for (int i = 0; i < xValues.Count(); i++) {
        int cntSeries = 0;
        for (int s = 0; s < diagram->GetSeriesCount(); s++) {
            DiagramCall::DiagramSeries *ds = diagram->GetSeries(s);
            const DiagramCall::DiagramMappable *dm = ds->GetMappable();
            if (!seriesVisible[s] || isCategoricalMappable(dm) != drawCategorical) {
                continue;
            }
            cntSeries++;
            localXIndexToGlobal[cntSeries - 1].SetCount(dm->GetDataCount());
            if (preparedData->Count() < cntSeries) {
                preparedData->Append(new vislib::PtrArray<vislib::math::Point<float, 3> >());
                preparedSeries.Append(ds);
                (*preparedData)[preparedData->Count() - 1]->SetCount(xValues.Count());
            }
            bool found = false;
            for (int j = 0; j < dm->GetDataCount(); j++) {
                bool ret;
                if (drawCategorical) {
                    ret = dm->GetAbscissaValue(j, 0, &tmpString);
                    if (ret) {
                        int idx = static_cast<int>(categories.IndexOf(tmpString));
                        x = static_cast<float>(idx);
                        if (idx == i) {
                            localXIndexToGlobal[cntSeries - 1][j] = xValues.IndexOf(x);
                            y = dm->GetOrdinateValue(j);
                            found = true;
                        }
                    } else {
                        // this is a hole! but where?
                    }
                } else {
                    ret = dm->GetAbscissaValue(j, 0, &x);
                    if (ret) {
                        if (x == xValues[i]) {
                            localXIndexToGlobal[cntSeries - 1][j] = i;
                            x -= xRange.First();
                            x /= xRange.Second() - xRange.First();
                            y = dm->GetOrdinateValue(j);
                            found = true;
                        }
                    } else {
                        // this is a hole! but where?
                    }
                }
                if (found) {
                    z = 0.0f;
                    //if (y < 1.0f) {
                    //    printf("Michael luegt und serie %s hat (%f, %f)\n", preparedSeries[cntSeries - 1]->GetName(), x, y);
                    //}
                    (*(*preparedData)[cntSeries - 1])[i] = new vislib::math::Point<float, 3>(x, y, z);
                    break;
                }
            }
            if (!found) {
                (*(*preparedData)[cntSeries - 1])[i] = NULL;
            }
        }
    }
#endif
    //for (int s = 0; s < preparedData->Count(); s++) {
    //    printf("series %u:", s);
    //    for (int i = 0; i < xValues.Count(); i++) {
    //        if ((*(*preparedData)[s])[i] != NULL) {
    //            printf("(%f,%f,%f),", (*(*preparedData)[s])[i]->GetX(), (*(*preparedData)[s])[i]->GetY(), (*(*preparedData)[s])[i]->GetZ());
    //        } else {
    //            printf("(NULL),");
    //        }
    //    }
    //    printf("\n");
    //}

    // now we could directly stack and normalize
    if (stack) {
        for (int i = 0; i < (int)xValues.Count(); i++) {
            float sum = 0.0f;
            for (int s = 0; s < (int)preparedData->Count(); s++) {
                if ((*(*preparedData)[s])[i] != NULL) {
                    float y = (*(*preparedData)[s])[i]->GetY();
                    (*(*preparedData)[s])[i]->SetZ(sum);
                    sum += y;
                    (*(*preparedData)[s])[i]->SetY(sum);
                }
            }
            maxYValues[i] = sum;
            if (sum > maxStackedY) {
                maxStackedY = sum;
            }
        }
    }
    float norm = yRange.Second() - yRange.First();
    float xNorm = xRange.Second() - xRange.First();
    norm = drawLog ? log10(norm) : norm;
    for (int i = 0; i < (int)xValues.Count(); i++) {
        for (int s = 0; s < (int)preparedData->Count(); s++) {
            if ((*(*preparedData)[s])[i] != NULL) {
                float x = (*(*preparedData)[s])[i]->GetX();
                float y = (*(*preparedData)[s])[i]->GetY();
                float z = (*(*preparedData)[s])[i]->GetZ();
                if (stack) {
                    if (normalize) {
                        y /= maxYValues[i];
                        z /= maxYValues[i];
                    } else {
                        y /= maxStackedY;
                        z /= maxStackedY;
                    }
                } else {
                    if (drawLog) {
                        y = log10(y - yRange.First()) / norm;
                        //z = log10(z - yRange.First()) / norm;
                    } else {
                        x = (x - xRange.First()) / xNorm;
                        y = (y - yRange.First()) / norm;
                        //z = (z - yRange.First()) / norm;
                    }
                }
                (*(*preparedData)[s])[i]->SetZ(z);
                (*(*preparedData)[s])[i]->SetY(y);
                (*(*preparedData)[s])[i]->SetX(x);
                x -= 0.5f;
                y -= 0.5f;
                this->pointData.push_back(this->screenSpaceMidPoint.GetX() + x*this->screenSpaceDiagramSize.GetWidth());
                this->pointData.push_back(this->screenSpaceMidPoint.GetY() + y*this->screenSpaceDiagramSize.GetHeight());
                this->pointData.push_back(data[this->colorColIdx+i*ft->GetColumnsCount()]);
                this->pointDataPoints.push_back(vislib::math::Point<float, 3>(this->screenSpaceMidPoint.GetX() + x*this->screenSpaceDiagramSize.GetWidth(),
                    this->screenSpaceMidPoint.GetY() + y*this->screenSpaceDiagramSize.GetHeight(), z));
            }
        }
    }
    if (!normalize && stack) {
        this->yRange.SetSecond(maxStackedY * (yRange.Second() - yRange.First()) + yRange.First());
    }
}

// EVIL EVIL HACK HACK
//void NVGDiagramRenderer::dump() {
//    vislib::sys::BufferedFile bf;
//    bf.Open("dumm.stat", vislib::sys::BufferedFile::WRITE_ONLY, vislib::sys::BufferedFile::SHARE_READ, vislib::sys::BufferedFile::CREATE_OVERWRITE);
//    for (int i = 0; i < (int)(*preparedData)[0]->Count(); i++) {
//        vislib::sys::WriteFormattedLineToFile(bf, "## Frame %u\n", i);
//        for (int s = 0; s < (int)preparedData->Count(); s++) {
//            if ((*(*preparedData)[s])[i] != NULL) {
//                vislib::sys::WriteFormattedLineToFile(bf, "#C %u %u\n", s + 1, static_cast<int>(
//                    vislib::math::Min(vislib::math::Max((*(*preparedData)[s])[i]->GetY() * 23000.0f / 70.0f, 3.0f), 20.0f)));
//            }
//        }
//    }
//    for (int s = 0; s < (int)preparedData->Count(); s++) {
//        for (int i = 0; i < (int)preparedSeries[s]->GetMarkerCount(); i++) {
//            // WARNING s is synchronized to global series counter since no series that cannot be drawn are added for proteins
//            // For the rest of the universe THIS IS WRONG
//            const protein_calls::DiagramCall::DiagramMarker *m = preparedSeries[s]->GetMarker(i);
//            if (m->GetType() == protein_calls::DiagramCall::DIAGRAM_MARKER_MERGE && m->GetUserData() != NULL) {
//                vislib::Array<int> *partners = reinterpret_cast<vislib::Array<int> *>(m->GetUserData());
//                for (int p = 0; p < (int)partners->Count(); p++) {
//                    int idx = localXIndexToGlobal[s][m->GetIndex()];
//                    vislib::sys::WriteFormattedLineToFile(bf, "#F %u[%u]=>%u[%u] %u\n", (*partners)[p] + 1, idx - 1, s + 1, idx, 3);
//                }
//            } else if (m->GetType() == protein_calls::DiagramCall::DIAGRAM_MARKER_SPLIT && m->GetUserData() != NULL) {
//                vislib::Array<int> *partners = reinterpret_cast<vislib::Array<int> *>(m->GetUserData());
//                for (int p = 0; p < (int)partners->Count(); p++) {
//                    //Log::DefaultLog.WriteMsg(Log::LEVEL_INFO, "#F %u[%u]=>%u[%u] %u", s + 1, m->GetIndex(), (*partners)[p] + 1, m->GetIndex() + 1, 3);
//                    int idx = localXIndexToGlobal[s][m->GetIndex()];
//                    vislib::sys::WriteFormattedLineToFile(bf, "#F %u[%u]=>%u[%u] %u\n", (*partners)[p] + 1, idx - 1, s + 1, idx, 3);
//                }
//            }
//        }
//    }
//    vislib::sys::WriteFormattedLineToFile(bf, "## MaxClustID %u\n", preparedData->Count());
//    bf.Close();
//}

void NVGDiagramRenderer::drawLineDiagram(float w, float h) {
    this->defineLayout(w, h);
    //float sh = h - 0.1f*h;
    //float sw = w - 0.1f*w;

    //if (sw < sh) std::swap(sw, sh);

    //float oh = (h / 2.0f) - (sh / 2.0f);
    //float ow = (w / 2.0f) - (sw / 2.0f);

    float aspect = this->aspectRatioParam.Param<param::FloatParam>()->Value();

    this->drawLegend(w, h);

    this->drawXAxis(DIAGRAM_XAXIS_FLOAT);

    int type = this->diagramTypeParam.Param<param::EnumParam>()->Value();
    switch (type) {
    case DIAGRAM_TYPE_LINE:
        prepareData(false, false, false);
        break;
    case DIAGRAM_TYPE_LINE_STACKED:
        prepareData(true, false, false);
        break;
    case DIAGRAM_TYPE_LINE_STACKED_NORMALIZED:
        prepareData(true, true, false);
        break;
    }

    this->drawYAxis();

    NVGcontext *ctx = static_cast<NVGcontext *>(this->nvgCtxt);
    nvgSave(ctx);
    //nvgScale(ctx, this->scaleX, this->scaleY);
    nvgTransform(ctx, this->transform.GetAt(0, 0), this->transform.GetAt(1, 0),
        this->transform.GetAt(0, 1), this->transform.GetAt(1, 1),
        this->transform.GetAt(0, 2), this->transform.GetAt(1, 2));

    nvgFillColor(ctx, nvgRGB(255, 0, 0));
    nvgBeginPath(ctx);
    nvgCircle(ctx, 0.0f, 0.0f, 20);
    nvgCircle(ctx, 0.0f, h, 20);
    nvgCircle(ctx, w, 0.0f, 20);
    nvgCircle(ctx, w, h, 20);
    nvgFill(ctx);

    for (int s = 0; s < (int)preparedData->Count(); s++) {
        if ((*preparedData)[s]->Count() < 2 || !this->selected[s]) {
            continue;
        }

        auto color = std::get<4>(this->columnSelectors[s]);

        nvgStrokeWidth(ctx, this->lineWidthParam.Param<core::param::FloatParam>()->Value());
        nvgStrokeColor(ctx, nvgRGBf(color[0], color[1], color[2]));
        nvgBeginPath(ctx);
        
        float hinz = (*(*preparedData)[s])[0]->GetX() - 0.5f;
        float kunz = (((*(*preparedData)[s])[0]->GetY())) - 0.5f;
        nvgMoveTo(ctx, this->screenSpaceMidPoint.GetX()+hinz*this->screenSpaceDiagramSize.GetWidth(),
            this->screenSpaceMidPoint.GetY()+kunz*this->screenSpaceDiagramSize.GetHeight());

        for (int i = 1; i < (int)(*preparedData)[s]->Count(); i++) {
            float hinz = (*(*preparedData)[s])[i]->GetX() - 0.5f;
            float kunz = (((*(*preparedData)[s])[i]->GetY())) - 0.5f;
            nvgLineTo(ctx, this->screenSpaceMidPoint.GetX() + hinz*this->screenSpaceDiagramSize.GetWidth(),
                this->screenSpaceMidPoint.GetY() + kunz*this->screenSpaceDiagramSize.GetHeight());
        }

        nvgStroke(ctx);
    }
    nvgRestore(ctx);

  //  bool drawCategorical = this->drawCategoricalParam.Param<param::EnumParam>()->Value() != 0;
  //  if (drawCategorical) {
  //      return;
  //  }
  //  float aspect = this->aspectRatioParam.Param<param::FloatParam>()->Value();
  //  //glDisable(GL_BLEND);
  //  this->drawXAxis(DIAGRAM_XAXIS_FLOAT);
  //  int type = this->diagramTypeParam.Param<param::EnumParam>()->Value();
  //  switch (type) {
  //  case DIAGRAM_TYPE_LINE:
  //      prepareData(false, false, drawCategorical);
  //      break;
  //  case DIAGRAM_TYPE_LINE_STACKED:
  //      prepareData(true, false, drawCategorical);
  //      break;
  //  case DIAGRAM_TYPE_LINE_STACKED_NORMALIZED:
  //      prepareData(true, true, drawCategorical);
  //      break;
  //  }
  //  this->drawYAxis();
  //  //this->drawLegend();

  //  // HACK HACK
  //  /*bool d = false;
  //  if (d) {
  //      this->dump();
  //  }*/

  //  NVGcontext *ctx = static_cast<NVGcontext *>(this->nvgCtxt);
  //  int joins[3] = {NVG_MITER, NVG_ROUND, NVG_BEVEL};
  //  int caps[3] = {NVG_BUTT, NVG_ROUND, NVG_SQUARE};
  //          nvgSave(ctx);

  //  //::glBlendFunc(GL_ONE, GL_ONE);
  //  //::glDisable(GL_DEPTH_TEST);
  //  GLenum drawMode = 0;
  //  if (this->diagramStyleParam.Param<param::EnumParam>()->Value() == DIAGRAM_STYLE_FILLED) {
  //      drawMode = GL_TRIANGLE_STRIP;
  //      //::glEnable(GL_BLEND);
  //  } else {
  //      drawMode = GL_LINE_STRIP;
  //      //::glDisable(GL_BLEND);
  //  }
  //  for (int s = 0; s < (int)preparedData->Count(); s++) {
  //      if ((*preparedData)[s]->Count() < 2) {
  //          continue;
  //      }
  //      //::glBegin(drawMode);
  //      /*if (selectedSeries == NULL || *selectedSeries == *preparedSeries[s]) {
  //          ::glColor4fv(preparedSeries[s]->GetColor().PeekComponents());
  //      } else*/ {
  //          //::glColor4fv(unselectedColor.PeekComponents());
  //          nvgStrokeWidth(ctx, 1.0f);
  //          nvgStrokeColor(ctx, nvgRGB(255, 255, 255));
  //          nvgLineCap(ctx, caps[1]);
  //          nvgLineJoin(ctx, joins[1]);

  //          nvgBeginPath(ctx);
  //      }
  //      nvgMoveTo(ctx, (*(*preparedData)[s])[0]->GetX()*aspect, (*(*preparedData)[s])[0]->GetY());
  //      for (int i = 1; i < (int)(*preparedData)[s]->Count(); i++) {
  //          if ((*(*preparedData)[s])[i] != NULL) {
  //              float hinz = (*(*preparedData)[s])[i]->GetX();
  //              float kunz = (*(*preparedData)[s])[i]->GetY();
  //              nvgLineTo(ctx, hinz*aspect, kunz);
  //              /*::glVertex2f((*(*preparedData)[s])[i]->GetX() * aspect, (*(*preparedData)[s])[i]->GetY());
  //              if (drawMode == GL_TRIANGLE_STRIP) {
  //                  ::glVertex2f((*(*preparedData)[s])[i]->GetX() * aspect, (*(*preparedData)[s])[i]->GetZ());
  //              }*/
  //          } else {
  //              /*::glEnd();
  //              ::glBegin(drawMode);*/

  //              nvgStroke(ctx);
  //              nvgBeginPath(ctx);
  //              nvgMoveTo(ctx, (*(*preparedData)[s])[i+1]->GetX()*aspect, (*(*preparedData)[s])[i+1]->GetY());
  //          }
  //      }
  //      //::glEnd();
  //      nvgStroke(ctx);
  //  }
  //      nvgRestore(ctx);
  //  /*::glEnable(GL_BLEND);
  //  ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  //  ::glEnable(GL_TEXTURE);
  //  ::glEnable(GL_TEXTURE_2D);*/
  //  //::glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  //  //::glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); // <--

  //  /*int showMarkers = this->showMarkersParam.Param<param::EnumParam>()->Value();
  //  if (showMarkers != DIAGRAM_MARKERS_SHOW_NONE) {
  //      for (int s = 0; s < (int)preparedData->Count(); s++) {
  //          if (showMarkers == DIAGRAM_MARKERS_SHOW_ALL || preparedSeries[s] == selectedSeries) {
  //              float markerSize = fontSize;
  //              for (int i = 0; i < (int)preparedSeries[s]->GetMarkerCount(); i++) {
  //                  const protein_calls::DiagramCall::DiagramMarker *m = preparedSeries[s]->GetMarker(i);
  //                  for (int j = 0; j < (int)this->markerTextures.Count(); j++) {
  //                      if (markerTextures[j].First() == m->GetType()) {
  //                          int idx = localXIndexToGlobal[s][m->GetIndex()];
  //                          if ((*(*preparedData)[s])[idx] == NULL) {
  //                              continue;
  //                          }
  //                          markerTextures[j].Second()->Bind();
  //                          float x = (*(*preparedData)[s])[idx]->X();
  //                          float y = (*(*preparedData)[s])[idx]->Y();
  //                          x *= aspect;
  //                          x -= markerSize / 2.0f;
  //                          y -= markerSize / 2.0f;
  //                          ::glBegin(GL_TRIANGLE_STRIP);
  //                          if (selectedSeries == NULL || *selectedSeries == *preparedSeries[s]) {
  //                              ::glColor4fv(preparedSeries[s]->GetColor().PeekComponents());
  //                          } else {
  //                              ::glColor4fv(unselectedColor.PeekComponents());
  //                          }
  //                          ::glTexCoord2f(0.0f, 1.0f);
  //                          ::glVertex3f(x, y, decorationDepth - 0.5f);
  //                          ::glTexCoord2f(0.0f, 0.0f);
  //                          ::glVertex3f(x, y + markerSize, decorationDepth - 0.5f);
  //                          ::glTexCoord2f(1.0f, 1.0f);
  //                          ::glVertex3f(x + markerSize, y, decorationDepth - 0.5f);
  //                          ::glTexCoord2f(1.0f, 0.0f);
  //                          ::glVertex3f(x + markerSize, y + markerSize, decorationDepth - 0.5f);
  //                          ::glEnd();
  //                          continue;
  //                      }
  //                  }
  //              }
  //          }
  //      }
  //  }*/
  ///*  ::glDisable(GL_TEXTURE);
  //  ::glDisable(GL_TEXTURE_2D);
  //  ::glBindTexture(GL_TEXTURE_2D, 0);*/
}

void NVGDiagramRenderer::drawColumnDiagram() {
    barWidth = 0.0f;

    glDisable(GL_BLEND);
    float aspect = this->aspectRatioParam.Param<param::FloatParam>()->Value();
    bool drawCategorical = this->drawCategoricalParam.Param<param::EnumParam>()->Value() != 0;
    int type = this->diagramTypeParam.Param<param::EnumParam>()->Value();

    switch (type) {
    case DIAGRAM_TYPE_COLUMN:
        prepareData(false, false, drawCategorical);
        break;
    case DIAGRAM_TYPE_COLUMN_STACKED:
        prepareData(true, false, drawCategorical);
        break;
    case DIAGRAM_TYPE_COLUMN_STACKED_NORMALIZED:
        prepareData(true, true, drawCategorical);
        break;
    }
    this->drawYAxis();
    //this->drawLegend();

    vislib::StringA tmpString;

    if (drawCategorical == false) {
        this->drawXAxis(DIAGRAM_XAXIS_INTEGRAL);
        barWidth = (xTickOff * barWidthRatio) / preparedData->Count();
    } else {
        this->drawXAxis(DIAGRAM_XAXIS_CATEGORICAL);
        barWidth = (xTickOff * barWidthRatio) / preparedData->Count();
    }
    if (type == DIAGRAM_TYPE_COLUMN_STACKED || type == DIAGRAM_TYPE_COLUMN_STACKED_NORMALIZED) {
        barWidth = xTickOff * barWidthRatio;
    }

    float dw = this->screenSpaceDiagramSize.GetWidth();
    float dh = this->screenSpaceDiagramSize.GetHeight();
    float mw = this->screenSpaceMidPoint.GetX();
    float mh = this->screenSpaceMidPoint.GetY();

    NVGcontext *ctx = static_cast<NVGcontext *>(this->nvgCtxt);
    nvgSave(ctx);

    nvgStrokeWidth(ctx, 2.0f);
    nvgStrokeColor(ctx, nvgRGB(255, 255, 255));

    nvgFillColor(ctx, nvgRGB(255, 255, 255));

    nvgBeginPath(ctx);


    ::glPushMatrix();
    ::glScalef(aspect, 1.0f, 1.0f);
    GLenum drawMode = 0;
    if (this->diagramStyleParam.Param<param::EnumParam>()->Value() == DIAGRAM_STYLE_FILLED) {
        drawMode = GL_TRIANGLE_STRIP;
    } else {
        drawMode = GL_LINE_STRIP;
    }
    for (int s = 0; s < (int)preparedData->Count(); s++) {
        float x, y, y1;
        for (int i = 0; i < (int)(*preparedData)[s]->Count(); i++) {
            if ((*(*preparedData)[s])[i] == NULL) {
                continue;
            }

            float hinz = (*(*preparedData)[s])[i]->GetX() - 0.5f;
            float kunz = (1.0f - ((*(*preparedData)[s])[i]->GetY())) - 0.5f;

            float bw = barWidth;
            //float bh = 

            getBarXY(s, i, type, &x, &y);
            y1 = (*(*preparedData)[s])[i]->GetZ();
            ::glBegin(drawMode);
            /*if (selectedSeries == NULL || *selectedSeries == *preparedSeries[s]) {
                ::glColor4fv(preparedSeries[s]->GetColor().PeekComponents());
            } else*/ {
                ::glColor4fv(unselectedColor.PeekComponents());
            }
            if (drawMode == GL_TRIANGLE_STRIP) {
                ::glVertex2f(x + barWidth * 0.1f, y);
                ::glVertex2f(x + barWidth * 0.1f, y1);
                ::glVertex2f(x + barWidth * 0.9f, y);
                ::glVertex2f(x + barWidth * 0.9f, y1);
                ::glEnd();
            } else {
                ::glVertex2f(x + barWidth * 0.1f, y);
                ::glVertex2f(x + barWidth * 0.1f, y1);
                ::glVertex2f(x + barWidth * 0.9f, y1);
                ::glVertex2f(x + barWidth * 0.9f, y);
                ::glVertex2f(x + barWidth * 0.1f, y);
                ::glEnd();
            }
        }
    }
    ::glPopMatrix();

    nvgFill(ctx);

    nvgRestore(ctx);
}
