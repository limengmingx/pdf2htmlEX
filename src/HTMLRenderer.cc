/*
 * HTMLRenderer.cc
 *
 * Copyright (C) 2011 by Hongliang TIAN(tatetian@gmail.com)
 * Copyright (C) 2012 by Lu Wang coolwanglu<at>gmail.com
 */

/*
 * TODO
 * color
 * transformation
 * remove line break for divs in the same line
 *
 * updatetextmat, position etc.
 *
 * font base64 embedding
 * custom css
 */

#include <cmath>
#include <cassert>
#include <fstream>
#include <algorithm>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

#include <GfxFont.h>
#include <UTF8.h>
#include <fofi/FoFiType1C.h>
#include <fofi/FoFiTrueType.h>
#include <splash/SplashBitmap.h>

#include "HTMLRenderer.h"
#include "BackgroundRenderer.h"

/*
 * CSS classes
 *
 * p - Page
 * t - Transform
 * l - Line
 * w - White space
 *
 * 
 * Reusable CSS classes
 *
 * f<hex> - Font (also for font names)
 * s<hex> - font Size
 * w<hex> - White space
 * t<hex> - Transform matrix (for both CTM and Text Matrix)
 */

const char * HTML_HEAD = "<!DOCTYPE html>\n\
<html><head>\
<meta charset=\"utf-8\">\
<style type=\"text/css\">\
#pdf-main {\
  font-family: sans-serif;\
  position:absolute;\
  top:0;\
  left:0;\
  bottom:0;\
  right:0;\
  overflow:auto;\
  background-color:grey;\
}\
#pdf-main > .p {\
  position:relative;\
  margin:13px auto;\
  background-color:white;\
  overflow:hidden;\
  display:none;\
}\
.p > .t {\
  position:absolute;\
  top:0;\
  left:0;\
  bottom:0;\
  right:0;\
  transform-origin:0% 100%;\
  -ms-transform-origin:0% 100%;\
  -moz-transform-origin:0% 100%;\
  -webkit-transform-origin:0% 100%;\
  -o-transform-origin:0% 100%;\
}\
.t > .l {\
  position:absolute; \
  white-space:pre;\
  transform-origin:0% 100%;\
  -ms-transform-origin:0% 100%;\
  -moz-transform-origin:0% 100%;\
  -webkit-transform-origin:0% 100%;\
  -o-transform-origin:0% 100%;\
}\
.l > .w{\
  display:inline-block;\
}\
::selection{\
  background: rgba(168,209,255,0.5);\
}\
::-moz-selection{\
  background: rgba(168,209,255,0.5);\
}\
</style><link rel=\"stylesheet\" type=\"text/css\" href=\"all.css\" />\
<script type=\"text/javascript\">\
function show_pages()\
{\
 var pages = document.getElementById('pdf-main').childNodes;\
 var idx = 0;\
 var f = function(){\
  if (idx < pages.length) {\
   try{\
    pages[idx].style.display='block';\
   }catch(e){}\
   ++idx;\
   setTimeout(f,100);\
  }\
 };\
 f();\
};\
</script>\
</head><body onload=\"show_pages();\"><div id=\"pdf-main\">";

const char * HTML_TAIL = "</div></body></html>";

const std::map<string, string> BASE_14_FONT_CSS_FONT_MAP({\
   { "Courier", "Courier,monospace" },\
   { "Helvetica", "Helvetica,Arial,\"Nimbus Sans L\",sans-serif" },\
   { "Times", "Times,\"Time New Roman\",\"Nimbus Roman No9 L\",serif" },\
   { "Symbol", "Symbol" },\
   { "ZapfDingbats", "ZapfDingbats" },\
});

TextString::TextString(GfxState *state)
    :unicodes()
    ,x(state->getCurX()), y(state->getCurY())
    ,width(0),height(0)
    ,state(state)
{ 
}

TextString::~TextString()
{
    delete state;
    state = nullptr;
}

void TextString::addChar(GfxState *state, double x, double y,
        double dx, double dy, Unicode u)
{
    if (0 < u && u != 9 && u < 32)	// skip non-printable not-tab character
        return;

    /*
    if (unicodes.empty())
    {
        this->x = x;
        this->y = y;
    }
    */
    unicodes.push_back(u);

    width += dx;
    height += dy;
}


HTMLRenderer::HTMLRenderer(const Param * param)
    :cur_string(nullptr), cur_line(nullptr)
    ,html_fout(param->output_filename.c_str(), ofstream::binary), allcss_fout("all.css")
    ,param(param)
{
    // install default font & size
    install_font(nullptr);
    install_font_size(0);

    html_fout << HTML_HEAD; 
    if(param->readable) html_fout << endl;
}

HTMLRenderer::~HTMLRenderer()
{
    html_fout << HTML_TAIL;
    if(param->readable) html_fout << endl;
}

void HTMLRenderer::process(PDFDoc *doc)
{
    xref = doc->getXRef();
    for(int i = param->first_page; i <= param->last_page ; ++i) 
    {
        doc->displayPage(this, i, param->h_dpi, param->v_dpi,
                0, true, false, false,
                nullptr, nullptr, nullptr, nullptr);
    }

    // Render non-text objects as image
    // copied from poppler
    SplashColor color;
    color[0] = color[1] = color[2] = 255;

    auto bg_renderer = new BackgroundRenderer(splashModeRGB8, 4, gFalse, color);
    bg_renderer->startDoc(doc);

    for(int i = param->first_page; i <= param->last_page ; ++i) 
    {
        doc->displayPage(bg_renderer, i, 4*param->h_dpi, 4*param->v_dpi,
                0, true, false, false,
                nullptr, nullptr, nullptr, nullptr);
        bg_renderer->getBitmap()->writeImgFile(splashFormatPng, (char*)(boost::format("p%|1$x|.png")%i).str().c_str(), 4*param->h_dpi, 4*param->v_dpi);
    }
    delete bg_renderer;
}

void HTMLRenderer::startPage(int pageNum, GfxState *state) 
{
    this->pageNum = pageNum;
    this->pageWidth = state->getPageWidth();
    this->pageHeight = state->getPageHeight();

    assert(cur_line == nullptr);
    assert(cur_string == nullptr);

    html_fout << boost::format("<div id=\"page-%3%\" class=\"p\" style=\"width:%1%px;height:%2%px;") % pageWidth % pageHeight % pageNum;

    html_fout << boost::format("background-image:url(p%|3$x|.png);background-position:0 0;background-size:%1%px %2%px;background-repeat:no-repeat;") % pageWidth % pageHeight % pageNum;
            
    html_fout << "\">";
    if(param->readable) html_fout << endl;

    cur_x = cur_y = 0;
    cur_fn_id = cur_fs_id = 0;
    cur_line_x_offset = 0;

    for(int i = 0; i < 6; ++i)
        cur_ctm[i] = cur_text_mat[i] = 0.0;
    cur_ctm[0] = cur_text_mat[0] = cur_ctm[3] = cur_text_mat[3] = 1.0;
    
    pos_changed = false;
    ctm_changed = false;
    text_mat_changed = false;
    font_changed = false;

    // default CTM
    html_fout << boost::format("<div class=\"t t%|1$x|\">") % install_transform_matrix(cur_ctm);
    if(param->readable) html_fout << endl;
}

void HTMLRenderer::endPage() {
    close_cur_line();
    // close CTM
    html_fout << "</div>";
    if(param->readable) html_fout << endl;
    // close page
    html_fout << "</div>";
    if(param->readable) html_fout << endl;
}

bool HTMLRenderer::at_same_line(const TextString * ts1, const TextString * ts2) const
{
    if(!(std::abs(ts1->getY() - ts2->getY()) < param->v_eps))
        return false;

    GfxState * s1 = ts1->getState();
    GfxState * s2 = ts2->getState();
    
    if(!(_equal(s1->getCharSpace(), s2->getCharSpace())
         && _equal(s1->getWordSpace(), s2->getWordSpace())
         && _equal(s1->getHorizScaling(), s2->getHorizScaling())))
            return false;

    /*
      no need for this, as we track TM now
    if(!(_tm_equal(s1->getCTM(), s2->getCTM()) && _tm_equal(s1->getTextMat(), s2->getTextMat())))
        return false;
        */

    return true;
}

void HTMLRenderer::close_cur_line()
{
    if(cur_line != nullptr)
    {
        html_fout << "</div>";
        if(param->readable) html_fout << endl;

        delete cur_line;
        cur_line = nullptr;
        cur_line_x_offset = 0;
    }
}

void HTMLRenderer::outputTextString(TextString * str)
{
    for (auto u : str->getUnicodes())
    {
        switch(u)
        {
            case '&':
                html_fout << "&amp;";
                break;
            case '\"':
                html_fout << "&quot;";
                break;
            case '\'':
                html_fout << "&apos;";
                break;
            case '<':
                html_fout << "&lt;";
                break;
            case '>':
                html_fout << "&gt;";
                break;
            default:
                {
                    char buf[4];
                    auto n = mapUTF8(u, buf, 4);
                    if(n > 0)
                        html_fout.write(buf, n);
                }
        }
    }
}

void HTMLRenderer::updateAll(GfxState *state)
{
    font_changed = true;
    text_mat_changed = true;
    ctm_changed = true;
    pos_changed = true;
}

void HTMLRenderer::updateFont(GfxState *state) 
{
    font_changed = true;
}

void HTMLRenderer::updateTextMat(GfxState * state)
{
    text_mat_changed = true;
}

void HTMLRenderer::updateCTM(GfxState * state, double m11, double m12, double m21, double m22, double m31, double m32)
{
    ctm_changed = true;
}

void HTMLRenderer::updateTextPos(GfxState * state)
{
    pos_changed = true;
}

void HTMLRenderer::saveTextPos(GfxState * state)
{
    cout << "save" << endl;
}

void HTMLRenderer::restoreTextPos(GfxState * state)
{
    cout << "restore" << endl;
}

void HTMLRenderer::beginString(GfxState *state, GooString *s) {
    check_state_change(state);

    // TODO: remove this
    GfxState * new_state = state->copy(gTrue);

    cur_string = new TextString(new_state);
}

void HTMLRenderer::endString(GfxState *state) {
    if (cur_string->getSize() == 0) {
        delete cur_string ;
        return;
    }

    // try to merge with last line
    if(cur_line != nullptr)
    {
        if(at_same_line(cur_line, cur_string))
        {
            double x1 = cur_line->getX() + cur_line->getWidth();
            double x2 = cur_string->getX();
            double target = x2-x1-cur_line_x_offset;

            if(target > -param->h_eps)
            {
                if(target > param->h_eps)
                {
                    double w;
                    auto wid = install_whitespace(target, w);
                    cur_line_x_offset = w-target;
                    html_fout << boost::format("<span class=\"w w%|1$x|\"> </span>") % wid;
                }
                else
                {
                    cur_line_x_offset = -target;
                }

                outputTextString(cur_string);

                delete cur_line;
                cur_line = cur_string;
                cur_string = nullptr;
                return;
            }
        }
    }

    close_cur_line();

    GfxState * cur_state = cur_string -> getState();

    // fix if font size too small
    long long new_fs_id;
    long long new_tm_id = 0;
    if((cur_font_size < 1) && _is_positive(cur_font_size))
    { 
        new_fs_id = install_font_size(1);

        double tmp_text_mat[6];
        memcpy(tmp_text_mat, cur_text_mat, sizeof(tmp_text_mat));
        tmp_text_mat[0] *= cur_font_size;
        tmp_text_mat[3] *= cur_font_size;
        new_tm_id = install_transform_matrix(tmp_text_mat);
    } 
    else
    {
        new_fs_id = cur_fs_id;
        new_tm_id = install_transform_matrix(cur_text_mat);
    }

    // TODO: optimize text matrix search/install
    // TODO: position might not be accurate
    html_fout << boost::format("<div class=\"l f%|1$x| s%|2$x| t%|3$x|\" style=\"") % cur_fn_id % new_fs_id % new_tm_id
        << "bottom:" << cur_string->getY() << "px;"
        << "left:" << cur_string->getX() << "px;"
        << "top:" << (pageHeight - cur_string->getY() - cur_state->getFont()->getAscent() * cur_state->getFontSize()) << "px;"
        ;
    
    // letter & word spacing
    if(_is_positive(cur_state->getCharSpace()))
        html_fout << "letter-spacing:" << cur_state->getCharSpace() << "px;";
    if(_is_positive(cur_state->getWordSpace()))
        html_fout << "word-spacing:" << cur_state->getWordSpace() << "px;";

    //debug 
    //real pos
    {
        html_fout << "\"";
        double x,y;
        cur_state->transform(cur_state->getCurX(), cur_state->getCurY(), &x, &y);
        html_fout << boost::format(" data-x=\"%1%\" data-y=\"%2%\" hs=\"%3%")%x%y%(cur_state->getHorizScaling());
    }

    html_fout << "\">";

    outputTextString(cur_string);

    cur_line = cur_string;
    cur_string = nullptr;
    cur_line_x_offset = 0;
}

void HTMLRenderer::drawChar(GfxState *state, double x, double y,
        double dx, double dy,
        double originX, double originY,
        CharCode code, int /*nBytes*/, Unicode *u, int uLen)
{
    double x1, y1, w1, h1;
    
    x1 = x;
    y1 = y;

    // if it is hidden, then return
    if ((state->getRender() & 3) == 3)
        return ;

    // TODO:
    // not on the same line
    if (!_equal(cur_string->getY(), y1)){
        std::cerr << "TODO: line break in a string" << std::endl;
    }

    w1 = dx - state->getCharSpace() * state->getHorizScaling(),

    h1 = dy;

    if (uLen != 0) {
        w1 /= uLen;
        h1 /= uLen;
    }

    for (int i = 0; i < uLen; ++i) {
        cur_string->addChar(state, x1 + i*w1, y1 + i*h1, w1, h1, u[i]);
    }
}

// TODO
void HTMLRenderer::drawString(GfxState * state, GooString * s)
{
    auto font = state->getFont();
    if(font->getWMode())
        std::cerr << "TODO: writing mode" << std::endl;

    // stolen from poppler
    double dx = 0; 
    double dy = 0;
    double dx2, dy2;
    double ox, oy;

    char *p = s->getCString();
    int len = s->getLength();
    int nChars = 0;
    int nSpaces = 0;
    int uLen;
    CharCode code;
    Unicode *u = nullptr;

    while (len > 0) {
        auto n = font->getNextChar(p, len, &code, &u, &uLen, &dx2, &dy2, &ox, &oy);
        dx += dx2;
        dy += dy2;
        if (n == 1 && *p == ' ') {
            ++nSpaces;
        }
        ++nChars;
        p += n;
        len -= n;
    }

    dx = dx * state->getFontSize()
        + nChars * state->getCharSpace()
        + nSpaces * state->getWordSpace();
    dx *= state->getHorizScaling();
    dy *= state->getFontSize();

}

// The font installation code is stolen from PSOutputDev.cc in poppler

long long HTMLRenderer::install_font(GfxFont * font)
{
    assert(sizeof(long long) == 2*sizeof(int));
                
    long long fn_id = (font == nullptr) ? 0 : *reinterpret_cast<long long*>(font->getID());
    auto iter = font_name_map.find(fn_id);
    if(iter != font_name_map.end())
        return iter->second.fn_id;

    long long new_fn_id = font_name_map.size(); 

    font_name_map.insert(std::make_pair(fn_id, FontInfo({new_fn_id})));

    if(font == nullptr)
    {
        export_remote_default_font(new_fn_id);
        return new_fn_id;
    }

    string new_fn = (boost::format("f%|1$x|") % new_fn_id).str();

    if(font->getType() == fontType3) {
        std::cerr << "Type 3 fonts are unsupported and will be rendered as Image" << std::endl;
        export_remote_default_font(new_fn_id);
        return new_fn_id;
    }
    if(font->getWMode()) {
        std::cerr << "Writing mode is unsupported and will be rendered as Image" << std::endl;
        export_remote_default_font(new_fn_id);
        return new_fn_id;
    }

    auto * font_loc = font->locateFont(xref, gTrue);
    if(font_loc != nullptr)
    {
        switch(font_loc -> locType)
        {
            case gfxFontLocEmbedded:
                switch(font_loc -> fontType)
                {
                    case fontType1:
                        install_embedded_type1_font(&font_loc->embFontID, new_fn_id);
                        break;
                    case fontType1C:
                        install_embedded_type1c_font(font, new_fn_id);
                        break;
                    case fontType1COT:
                        install_embedded_opentypet1c_font(font, new_fn_id);
                        break;
                    case fontTrueType:
                    case fontTrueTypeOT:
                        install_embedded_truetype_font(font, new_fn_id);
                        break;
                    default:
                        std::cerr << "TODO: unsuppported embedded font type" << std::endl;
                        export_remote_default_font(new_fn_id);
                        break;
                }
                break;
            case gfxFontLocExternal:
                std::cerr << "TODO: external font" << std::endl;
                export_remote_default_font(new_fn_id);
                break;
            case gfxFontLocResident:
                install_base_font(font, font_loc, new_fn_id);
                break;
            default:
                std::cerr << "TODO: other font loc" << std::endl;
                export_remote_default_font(new_fn_id);
                break;
        }      

        delete font_loc;
    }

      
    return new_fn_id;
}

void HTMLRenderer::install_embedded_type1_font (Ref * id, long long fn_id)
{
    Object ref_obj, str_obj, ol1, ol2, ol3;
    Dict * dict;
    
    int l1, l2, l3;
    int c;
    bool is_bin = false;
    int buf[4];

    ofstream tmpf((boost::format("f%|1$x|.pfa")%fn_id).str().c_str(), ofstream::binary);
    auto output_char = [&tmpf](int c)->void {
        char tmp = (char)(c & 0xff);
        tmpf.write(&tmp, 1);
    };

    ref_obj.initRef(id->num, id->gen);
    ref_obj.fetch(xref, &str_obj);
    ref_obj.free();

    if(!str_obj.isStream())
    {
        std::cerr << "Embedded font is not a stream" << std::endl;
        goto err;
    }

    dict = str_obj.streamGetDict();
    if(dict == nullptr)
    {
        std::cerr << "No dict in the embedded font" << std::endl;
        goto err;
    }

    dict->lookup("Length1", &ol1);
    dict->lookup("Length2", &ol2);
    dict->lookup("Length3", &ol3);

    if(!(ol1.isInt() && ol2.isInt() && ol3.isInt()))
    {
        std::cerr << "Length 1&2&3 are not all integers" << std::endl;
        ol1.free();
        ol2.free();
        ol3.free();
        goto err;
    }

    l1 = ol1.getInt();
    l2 = ol2.getInt();
    l3 = ol3.getInt();
    ol1.free();
    ol2.free();
    ol3.free();

    str_obj.streamReset();
    for(int i = 0; i < l1; ++i)
    {
        if((c = str_obj.streamGetChar()) == EOF)
            break;
        output_char(c);
    }

    if(l2 == 0)
    {
        std::cerr << "Bad Length2" << std::endl;
        goto err;
    }
    {
        int i;
        for(i = 0; i < 4; ++i)
        {
            int j = buf[i] = str_obj.streamGetChar();
            if(buf[i] == EOF)
            {
                std::cerr << "Embedded font stream is too short" << std::endl;
                goto err;
            }

            if(!((j>='0'&&j<='9') || (j>='a'&&j<='f') || (j>='A'&&j<='F')))
            {
                is_bin = true;
                ++i;
                break;
            }
        }
        if(is_bin)
        {
            static const char hex_char[] = "0123456789ABCDEF";
            for(int j = 0; j < i; ++j)
            {
                output_char(hex_char[(buf[j]>>4)&0xf]);
                output_char(hex_char[buf[j]&0xf]);
            }
            for(; i < l2; ++i)
            {
                if(i % 32 == 0)
                    output_char('\n');
                int c = str_obj.streamGetChar();
                if(c == EOF)
                    break;
                output_char(hex_char[(c>>4)&0xf]);
                output_char(hex_char[c&0xf]);
            }
            if(i % 32 != 0)
                output_char('\n');
        }
        else
        {
            for(int j = 0; j < i; ++j)
            {
                output_char(buf[j]);
            }
            for(;i < l2; ++i)
            {
                int c = str_obj.streamGetChar();
                if(c == EOF)
                    break;
                output_char(c);
            }
        }
    }
    if(l3 > 0)
    {
        int c;
        while((c = str_obj.streamGetChar()) != EOF)
            output_char(c);
    }
    else
    {
        for(int i = 0; i < 8; ++i)
        {
            for(int j = 0; j < 64; ++j)
                output_char('0');
            output_char('\n');
        }
        static const char * CTM = "cleartomark\n";
        tmpf.write(CTM, strlen(CTM));
    }

    export_remote_font(fn_id, "ttf");

err:
    str_obj.streamClose();
    str_obj.free();
}

void HTMLRenderer::output_to_file(void * outf, const char * data, int len)
{
    reinterpret_cast<ofstream*>(outf)->write(data, len);
}

void HTMLRenderer::install_embedded_type1c_font (GfxFont * font, long long fn_id)
{
    int font_len;
    char * font_buf = font->readEmbFontFile(xref, &font_len);
    if(font_buf != nullptr)
    {
        auto * FFT1C = FoFiType1C::make(font_buf, font_len);
        if(FFT1C != nullptr)
        {
            string fn = (boost::format("f%|1$x|")%fn_id).str();
            ofstream tmpf((fn+".pfa").c_str(), ofstream::binary);
            FFT1C->convertToType1((char*)fn.c_str(), nullptr, true, &output_to_file , &tmpf);
            export_remote_font(fn_id, "ttf");
            delete FFT1C;
        }
        else
        {
            std::cerr << "Warning: cannot process type 1c font: " << fn_id << std::endl;
            export_remote_default_font(fn_id);
        }
        gfree(font_buf);
    }
}

void HTMLRenderer::install_embedded_opentypet1c_font (GfxFont * font, long long fn_id)
{
    install_embedded_truetype_font(font, fn_id);
}

void HTMLRenderer::install_embedded_truetype_font (GfxFont * font, long long fn_id)
{
    int font_len;
    char * font_buf = font->readEmbFontFile(xref, &font_len);
    if(font_buf != nullptr)
    {
        auto * FFTT = FoFiTrueType::make(font_buf, font_len);
        if(FFTT != nullptr)
        {
            string fn = (boost::format("f%|1$x|")%fn_id).str();
            ofstream tmpf((fn+".ttf").c_str(), ofstream::binary);
            FFTT->writeTTF(output_to_file, &tmpf, (char*)(fn.c_str()), nullptr);
            export_remote_font(fn_id, "ttf");
            delete FFTT;
        }
        else
        {
            std::cerr << "Warning: cannot process truetype (or opentype t1c) font: " << fn_id << std::endl;
            export_remote_default_font(fn_id);
        }
        gfree(font_buf);
    }
}
void HTMLRenderer::install_base_font( GfxFont * font, GfxFontLoc * font_loc, long long fn_id)
{
    std::string psname(font_loc->path->getCString());
    string basename = psname.substr(0, psname.find('-'));
    string cssfont;
    auto iter = BASE_14_FONT_CSS_FONT_MAP.find(basename);
    if(iter == BASE_14_FONT_CSS_FONT_MAP.end())
    {
        std::cerr << "PS Font: " << basename << " not found in the base 14 font map" << std::endl;
        cssfont = "";
    }
    else
        cssfont = iter->second;

    export_local_font(fn_id, font, font_loc, psname, cssfont);
}

long long HTMLRenderer::install_font_size(double font_size)
{
    auto iter = font_size_map.lower_bound(font_size - EPS);
    if((iter != font_size_map.end()) && (_equal(iter->first, font_size)))
        return iter->second;

    long long new_fs_id = font_size_map.size();
    font_size_map.insert(std::make_pair(font_size, new_fs_id));
    export_font_size(new_fs_id, font_size);
    return new_fs_id;
}

long long HTMLRenderer::install_whitespace(double ws_width, double & actual_width)
{
    auto iter = whitespace_map.lower_bound(ws_width - param->h_eps);
    if((iter != whitespace_map.end()) && (std::abs(iter->first - ws_width) < param->h_eps))
    {
        actual_width = iter->first;
        return iter->second;
    }

    actual_width = ws_width;
    long long new_ws_id = whitespace_map.size();
    whitespace_map.insert(std::make_pair(ws_width, new_ws_id));
    export_whitespace(new_ws_id, ws_width);
    return new_ws_id;
}

long long HTMLRenderer::install_transform_matrix(double * tm){
    TM m(tm);
    auto iter = transform_matrix_map.lower_bound(m);
    if(m == (iter->first))
    {
        return iter->second;
    }

    long long new_tm_id = transform_matrix_map.size();
    transform_matrix_map.insert(std::make_pair(m, new_tm_id));
    export_transform_matrix(new_tm_id, tm);
    return new_tm_id;
}


void HTMLRenderer::export_remote_font(long long fn_id, const string & suffix)
{
    allcss_fout << boost::format("@font-face{font-family:f%|1$x|;src:url(f%|1$x|.%2%);}.f%|1$x|{font-family:f%|1$x|;}") % fn_id % suffix;
    if(param->readable) allcss_fout << endl;
}

void HTMLRenderer::export_remote_default_font(long long fn_id)
{
    allcss_fout << boost::format(".f%|1$x|{font-family:sans-serif;color:transparent;}")%fn_id;
    if(param->readable) allcss_fout << endl;
}

void HTMLRenderer::export_local_font(long long fn_id, GfxFont * font, GfxFontLoc * font_loc, const string & original_font_name, const string & cssfont)
{
    allcss_fout << boost::format(".f%|1$x|{") % fn_id;
    allcss_fout << "font-family:" << ((cssfont == "") ? (original_font_name + "," + general_font_family(font)) : cssfont) << ";";

    if(font->isBold())
        allcss_fout << "font-weight:bold;";

    if(boost::algorithm::ifind_first(original_font_name, "oblique"))
        allcss_fout << "font-style:oblique;";
    else if(font->isItalic())
        allcss_fout << "font-style:italic;";

    allcss_fout << "}";

    if(param->readable) allcss_fout << endl;
}

std::string HTMLRenderer::general_font_family(GfxFont * font)
{
    if(font -> isFixedWidth())
        return "monospace";
    else if (font -> isSerif())
        return "serif";
    else
        return "sans-serif";
}

void HTMLRenderer::export_font_size (long long fs_id, double font_size)
{
    allcss_fout << boost::format(".s%|1$x|{font-size:%2%px;}") % fs_id % font_size;
    if(param->readable) allcss_fout << endl;
}

void HTMLRenderer::export_whitespace (long long ws_id, double ws_width)
{
    allcss_fout << boost::format(".w%|1$x|{width:%2%px;}") % ws_id % ws_width;
    if(param->readable) allcss_fout << endl;
}

void HTMLRenderer::export_transform_matrix (long long tm_id, double * tm)
{
    allcss_fout << boost::format(".t%|1$x|{") % tm_id;


    // TODO: recognize common matices
    static const double id_matrix[6] = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    if(_tm_equal(tm, id_matrix))
    {
        // no need to output anything
    }
    else
    {
        for(const std::string & prefix : {"", "-ms-", "-moz-", "-webkit-", "-o-"})
        {
            // PDF use a different coordinate system from Web
            allcss_fout << prefix << "transform:matrix("
                << tm[0] << ','
                << -tm[1] << ','
                << -tm[2] << ','
                << tm[3] << ',';

            if(prefix == "-moz-")
                allcss_fout << boost::format("%1%px,%2%px);") % tm[4] % -tm[5];
            else
                allcss_fout << boost::format("%1%,%2%);") % tm[4] % -tm[5];
        }
    }
    allcss_fout << "}";
    if(param->readable) allcss_fout << endl;

}

void HTMLRenderer::check_state_change(GfxState * state)
{
    if(pos_changed)
    {
        if(!(_equal(state->getCurX(), cur_x) && _equal(state->getCurY(), cur_y)))
        {
            close_cur_line();
            cur_x = state->getCurX();
            cur_y = state->getCurY();
        }
        pos_changed = false;
    }

    if(font_changed)
    {
        long long new_fn_id = install_font(state->getFont());
        long long new_fs_id = install_font_size(state->getFontSize());
        cur_font_size = state->getFontSize();
        if(!((new_fn_id == cur_fn_id) && (new_fs_id == cur_fs_id)))
        {
            close_cur_line();
            cur_fn_id = new_fn_id;
            cur_fs_id = new_fs_id;
        }
        font_changed = false;
    }  
    if(text_mat_changed)
    {
        if(!_tm_equal(cur_text_mat, state->getTextMat(), 4))
        {
            close_cur_line();
            memcpy(cur_text_mat, state->getTextMat(), sizeof(cur_text_mat));

            // we've already shift the text to the correct posstion
            // so later in css we need to ignore the these offsets
            cur_text_mat[4] = cur_text_mat[5] = 0.0;
        }
        text_mat_changed = false;
    }

    if(ctm_changed)
    {
        if(!_tm_equal(cur_ctm, state->getCTM()))
        {
            close_cur_line();
            memcpy(cur_ctm, state->getCTM(), sizeof(cur_ctm));

            // close old CTM div and create a new one
            html_fout << "</div>";
            if(param->readable) html_fout << endl;
            html_fout << boost::format("<div class=\"t t%|1$x|\">") % install_transform_matrix(cur_ctm);
            if(param->readable) html_fout << endl;
        }
        ctm_changed = false;
    }
}

