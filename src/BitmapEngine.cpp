#include "BitmapEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <sstream>

namespace
{
    static WORD ReadU16(const std::vector<BYTE>& d, size_t o) { return static_cast<WORD>(d[o] | (static_cast<WORD>(d[o+1]) << 8)); }
    static DWORD ReadU32(const std::vector<BYTE>& d, size_t o) { return static_cast<DWORD>(d[o]) | (static_cast<DWORD>(d[o+1])<<8) | (static_cast<DWORD>(d[o+2])<<16) | (static_cast<DWORD>(d[o+3])<<24); }
    static LONG ReadS32(const std::vector<BYTE>& d, size_t o) { return static_cast<LONG>(ReadU32(d,o)); }
    static void WriteU16(FILE* f, WORD v) { BYTE b[2]={(BYTE)(v&255),(BYTE)((v>>8)&255)}; fwrite(b,1,2,f); }
    static void WriteU32(FILE* f, DWORD v) { BYTE b[4]={(BYTE)(v&255),(BYTE)((v>>8)&255),(BYTE)((v>>16)&255),(BYTE)((v>>24)&255)}; fwrite(b,1,4,f); }
    static double Clamp01(double x){ return x<0?0:(x>1?1:x); }
    static double WrapHue(double h){ while(h<0) h+=360; while(h>=360) h-=360; return h; }
    static double HueDistance(double a,double b){ double d=fabs(a-b); return d>180?360-d:d; }

    static void RgbToHsl(BYTE rr,BYTE gg,BYTE bb,double& h,double& s,double& l)
    {
        double r=rr/255.0,g=gg/255.0,b=bb/255.0,maxv=(std::max)(r,(std::max)(g,b)),minv=(std::min)(r,(std::min)(g,b)),d=maxv-minv;
        l=(maxv+minv)*0.5; if(d<1e-9){h=0;s=0;return;} s=d/(1.0-fabs(2.0*l-1.0));
        if(maxv==r) h=60.0*fmod((g-b)/d,6.0); else if(maxv==g) h=60.0*((b-r)/d+2.0); else h=60.0*((r-g)/d+4.0); h=WrapHue(h);
    }
    static double HueToRgb(double p,double q,double t){ if(t<0)t+=1;if(t>1)t-=1;if(t<1.0/6)return p+(q-p)*6*t;if(t<.5)return q;if(t<2.0/3)return p+(q-p)*(2.0/3-t)*6;return p; }
    static void HslToRgb(double h,double s,double l,BYTE& rr,BYTE& gg,BYTE& bb)
    {
        h=WrapHue(h)/360.0;s=Clamp01(s);l=Clamp01(l);double r,g,b;
        if(s<1e-9) r=g=b=l; else { double q=l<.5?l*(1+s):l+s-l*s,p=2*l-q; r=HueToRgb(p,q,h+1.0/3);g=HueToRgb(p,q,h);b=HueToRgb(p,q,h-1.0/3); }
        rr=(BYTE)(Clamp01(r)*255+.5);gg=(BYTE)(Clamp01(g)*255+.5);bb=(BYTE)(Clamp01(b)*255+.5);
    }
    static DWORD ColorKey(BYTE r,BYTE g,BYTE b){ return ((DWORD)r<<16)|((DWORD)g<<8)|b; }

    struct HueHistogram
    {
        double bins[72]; HueHistogram(){for(int i=0;i<72;++i)bins[i]=0;}
        void Add(BYTE r,BYTE g,BYTE b,double weight){double h,s,l;RgbToHsl(r,g,b,h,s,l);if(s<.20||l<.025||l>.975)return;bins[((int)(h/5.0))%72]+=weight*s;}
        bool Dominant(double& hue) const {int best=-1;double score=0;for(int i=0;i<72;++i)if(bins[i]>score){score=bins[i];best=i;}if(best<0)return false;double x=0,y=0,total=0;for(int k=-2;k<=2;++k){int i=(best+k+72)%72;double a=(i*5.0+2.5)*3.14159265358979323846/180.0,w=bins[i];x+=cos(a)*w;y+=sin(a)*w;total+=w;}if(total<=0)return false;hue=atan2(y,x)*180.0/3.14159265358979323846;if(hue<0)hue+=360;return true;}
    };

    static bool DecodeIndices(const std::vector<BYTE>& d,const BitmapInfoSummary& info,std::vector<BYTE>& idx,std::wstring& err)
    {
        size_t w=(size_t)info.width,h=info.height<0?(size_t)-info.height:(size_t)info.height; idx.assign(w*h,0);
        if(info.compression==BI_RGB)
        {
            for(size_t y=0;y<h;++y){const BYTE* row=&d[info.pixelOffset+y*info.rowStride];for(size_t x=0;x<w;++x){if(info.bitCount==8)idx[y*w+x]=row[x];else if(info.bitCount==4){BYTE v=row[x/2];idx[y*w+x]=(x&1)?(v&15):(v>>4);}else if(info.bitCount==1){BYTE v=row[x/8];idx[y*w+x]=(v>>(7-(x&7)))&1;}}}return true;
        }
        if(info.compression==BI_RLE4 && info.bitCount==4)
        {
            size_t p=info.pixelOffset,x=0,y=0; while(p<d.size() && y<h){BYTE c=d[p++];if(p>=d.size())break;BYTE v=d[p++];if(c){for(unsigned int n=0;n<c && y<h;++n){BYTE q=(n&1)?(v&15):(v>>4);if(x<w)idx[y*w+x]=q;if(++x>=w){x=0;++y;}}}else if(v==0){x=0;++y;}else if(v==1){break;}else if(v==2){if(p+1>=d.size())break;x+=d[p++];y+=d[p++];}else{unsigned int n=v,bytes=(n+1)/2;if(p+bytes>d.size())break;for(unsigned int j=0;j<n && y<h;++j){BYTE q=(j&1)?(d[p+j/2]&15):(d[p+j/2]>>4);if(x<w)idx[y*w+x]=q;if(++x>=w){x=0;++y;}}p+=bytes;if(bytes&1)++p;}}
            return true;
        }
        err=L"Indexed decoder supports BI_RGB and 4-bpp BI_RLE4.";return false;
    }

    static bool GetPixels(const std::vector<BYTE>& d,const BitmapInfoSummary& info,std::vector<DWORD>& colors,std::wstring& err)
    {
        size_t w=(size_t)info.width,h=info.height<0?(size_t)-info.height:(size_t)info.height; colors.assign(w*h,0);
        if(info.bitCount<=8){std::vector<BYTE> idx;if(!DecodeIndices(d,info,idx,err))return false;for(size_t i=0;i<idx.size();++i){size_t o=info.headerSize+(size_t)idx[i]*4;if(o+3>=d.size())return false;colors[i]=ColorKey(d[o+2],d[o+1],d[o]);}return true;}
        if(info.compression!=BI_RGB){err=L"Direct-color comparison requires BI_RGB.";return false;} size_t bpp=info.bitCount/8;
        for(size_t y=0;y<h;++y){const BYTE* row=&d[info.pixelOffset+y*info.rowStride];for(size_t x=0;x<w;++x){const BYTE* p=row+x*bpp;colors[y*w+x]=ColorKey(p[2],p[1],p[0]);}}return true;
    }
}

bool BitmapEngine::IsMagentaKey(BYTE r,BYTE g,BYTE b){return r>=245&&b>=245&&g<=20;}

bool BitmapEngine::ParseDib(const std::vector<BYTE>& dib,BitmapInfoSummary& info,std::wstring& errorText)
{
    if(dib.size()<40){errorText=L"DIB resource is too small for a BITMAPINFOHEADER.";return false;} DWORD hs=ReadU32(dib,0);if(hs<40||hs>dib.size()){errorText=L"Unsupported or truncated DIB header.";return false;}
    LONG w=ReadS32(dib,4),h=ReadS32(dib,8);WORD planes=ReadU16(dib,12),bpp=ReadU16(dib,14);DWORD comp=ReadU32(dib,16),sizeImage=ReadU32(dib,20),clr=ReadU32(dib,32);
    if(planes!=1||w<=0||h==0){errorText=L"Invalid DIB dimensions or plane count.";return false;} if(bpp!=1&&bpp!=4&&bpp!=8&&bpp!=24&&bpp!=32){std::wstringstream s;s<<L"v2.1 supports 1, 4, 8, 24 and 32 bpp bitmaps; resource is "<<bpp<<L" bpp.";errorText=s.str();return false;}
    if(comp!=BI_RGB && !(comp==BI_RLE4&&bpp==4)){errorText=L"v2.1 supports BI_RGB and 4-bpp BI_RLE4.";return false;}
    size_t pal=bpp<=8?(clr?clr:((size_t)1<<bpp)):0,off=(size_t)hs+pal*4;size_t ah=h<0?(size_t)-h:(size_t)h,row=((size_t)w*bpp+31)/32*4,pix=comp==BI_RGB?row*ah:(sizeImage?sizeImage:(dib.size()>off?dib.size()-off:0));
    if(off>dib.size()||pix>dib.size()-off){errorText=L"DIB pixel array extends past resource payload.";return false;} info.width=w;info.height=h;info.bitCount=bpp;info.compression=comp;info.headerSize=hs;info.colorsUsed=clr;info.paletteEntries=pal;info.pixelOffset=off;info.rowStride=row;info.pixelBytes=pix;return true;
}

bool BitmapEngine::AnalyzeDib(const std::vector<BYTE>& dib,BitmapAnalysis& a,std::wstring& errorText)
{
    BitmapInfoSummary info;if(!ParseDib(dib,info,errorText))return false;a.paletteEntries=(unsigned int)info.paletteEntries;a.usedPaletteEntries=0;a.saturatedUsedEntries=0;a.totalPixels=0;a.coloredPixels=0;a.uniqueUsedColors=0;std::vector<DWORD> colors;if(!GetPixels(dib,info,colors,errorText))return false;a.totalPixels=(unsigned long)colors.size();std::set<DWORD> unique;std::set<unsigned int> used;
    if(info.bitCount<=8){std::vector<BYTE> idx;if(!DecodeIndices(dib,info,idx,errorText))return false;for(size_t i=0;i<idx.size();++i)used.insert(idx[i]);a.usedPaletteEntries=(unsigned int)used.size();for(std::set<unsigned int>::const_iterator it=used.begin();it!=used.end();++it){size_t o=info.headerSize+(size_t)(*it)*4;double h,s,l;RgbToHsl(dib[o+2],dib[o+1],dib[o],h,s,l);if(s>=.20&&!IsMagentaKey(dib[o+2],dib[o+1],dib[o]))++a.saturatedUsedEntries;}}
    for(size_t i=0;i<colors.size();++i){unique.insert(colors[i]);BYTE r=(BYTE)(colors[i]>>16),g=(BYTE)(colors[i]>>8),b=(BYTE)colors[i];double h,s,l;RgbToHsl(r,g,b,h,s,l);if(s>=.20&&!IsMagentaKey(r,g,b))++a.coloredPixels;}a.uniqueUsedColors=(unsigned int)unique.size();return true;
}

bool BitmapEngine::WriteBmpFile(const std::wstring& path,const std::vector<BYTE>& dib,std::wstring& errorText)
{
    BitmapInfoSummary info;if(!ParseDib(dib,info,errorText))return false;FILE* f=NULL;if(_wfopen_s(&f,path.c_str(),L"wb")!=0||!f){errorText=L"Could not create output BMP file.";return false;}WriteU16(f,0x4D42);WriteU32(f,(DWORD)(14+dib.size()));WriteU16(f,0);WriteU16(f,0);WriteU32(f,(DWORD)(14+info.pixelOffset));if(!dib.empty())fwrite(&dib[0],1,dib.size(),f);fclose(f);return true;
}

bool BitmapEngine::LoadBmpFile(const std::wstring& path,std::vector<BYTE>& dib,std::wstring& errorText)
{
    FILE* f=NULL;if(_wfopen_s(&f,path.c_str(),L"rb")!=0||!f){errorText=L"Could not open BMP file.";return false;}fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);if(n<14){fclose(f);errorText=L"BMP file is too small.";return false;}std::vector<BYTE> all((size_t)n);if(fread(&all[0],1,all.size(),f)!=all.size()){fclose(f);errorText=L"Could not read BMP file.";return false;}fclose(f);if(all[0]!='B'||all[1]!='M'){errorText=L"Not a Windows BMP file.";return false;}dib.assign(all.begin()+14,all.end());BitmapInfoSummary i;return ParseDib(dib,i,errorText);
}

bool BitmapEngine::CompareDibs(const std::vector<BYTE>& first,const std::vector<BYTE>& second,BitmapCompareResult& r,std::wstring& errorText)
{
    BitmapInfoSummary a,b;if(!ParseDib(first,a,errorText)||!ParseDib(second,b,errorText))return false;r.compatible=a.width==b.width&&a.height==b.height;if(!r.compatible){errorText=L"Bitmap dimensions differ.";return false;}std::vector<DWORD> ca,cb;if(!GetPixels(first,a,ca,errorText)||!GetPixels(second,b,cb,errorText))return false;std::set<DWORD> ua(ca.begin(),ca.end()),ub(cb.begin(),cb.end());r.comparedPixels=(unsigned long)ca.size();r.differentPixels=0;r.firstUniqueColors=(unsigned int)ua.size();r.secondUniqueColors=(unsigned int)ub.size();double total=0;for(size_t i=0;i<ca.size();++i){int ar=(ca[i]>>16)&255,ag=(ca[i]>>8)&255,ab=ca[i]&255,br=(cb[i]>>16)&255,bg=(cb[i]>>8)&255,bb=cb[i]&255;if(ca[i]!=cb[i])++r.differentPixels;double dr=ar-br,dg=ag-bg,db=ab-bb;total+=sqrt(dr*dr+dg*dg+db*db);}r.averageRgbDistance=ca.empty()?0:total/ca.size();return true;
}

bool BitmapEngine::ParseColor(const std::wstring& text,BYTE& r,BYTE& g,BYTE& b,std::wstring& errorText)
{
    std::wstring s=text;if(!s.empty()&&s[0]==L'#')s.erase(0,1);if(s.size()!=6){errorText=L"Color must be #RRGGBB or RRGGBB.";return false;}wchar_t* e=NULL;unsigned long v=wcstoul(s.c_str(),&e,16);if(!e||*e){errorText=L"Color contains non-hexadecimal characters.";return false;}r=(BYTE)(v>>16);g=(BYTE)(v>>8);b=(BYTE)v;return true;
}

bool BitmapEngine::RecolorDominantHue(std::vector<BYTE>& dib,BYTE tr,BYTE tg,BYTE tb,double window,RecolorResult& result,std::wstring& errorText)
{
    result.changed=false;result.paletteEntries=0;result.usedPaletteEntries=0;result.changedColors=0;result.changedPixels=0;result.sourceHueDegrees=0;result.targetHueDegrees=0;result.note.clear();BitmapInfoSummary info;if(!ParseDib(dib,info,errorText))return false;
    double th,ts,tl;RgbToHsl(tr,tg,tb,th,ts,tl);result.targetHueDegrees=th;HueHistogram hist;size_t ah=info.height<0?(size_t)-info.height:(size_t)info.height;std::vector<unsigned long> usage(info.paletteEntries,0);std::vector<BYTE> idx;
    if(info.bitCount<=8){if(!DecodeIndices(dib,info,idx,errorText))return false;for(size_t i=0;i<idx.size();++i)if(idx[i]<usage.size())usage[idx[i]]++;for(size_t i=0;i<usage.size();++i)if(usage[i]){++result.usedPaletteEntries;size_t o=info.headerSize+i*4;BYTE b=dib[o],g=dib[o+1],r=dib[o+2];if(!IsMagentaKey(r,g,b))hist.Add(r,g,b,(double)usage[i]);}result.paletteEntries=(unsigned int)info.paletteEntries;}
    else {size_t bp=info.bitCount/8;for(size_t y=0;y<ah;++y){const BYTE* row=&dib[info.pixelOffset+y*info.rowStride];for(LONG x=0;x<info.width;++x){const BYTE* p=row+(size_t)x*bp;BYTE b=p[0],g=p[1],r=p[2];if(!IsMagentaKey(r,g,b))hist.Add(r,g,b,1);}}}
    double sh=0;if(!hist.Dominant(sh)){result.note=L"No sufficiently saturated dominant hue was found; resource left unchanged.";return true;}result.sourceHueDegrees=sh;if(window<5)window=5;if(window>180)window=180;double delta=WrapHue(th-sh);if(delta>180)delta-=360;
    if(info.bitCount<=8){for(size_t i=0;i<info.paletteEntries;++i){if(!usage[i])continue;size_t o=info.headerSize+i*4;BYTE b=dib[o],g=dib[o+1],r=dib[o+2];if(IsMagentaKey(r,g,b))continue;double h,s,l;RgbToHsl(r,g,b,h,s,l);if(s<.18||HueDistance(h,sh)>window)continue;double relative=WrapHue(h-sh);if(relative>180)relative-=360;double nh=WrapHue(th+relative);double ns=ts<.05?0:s;BYTE nr,ng,nb;HslToRgb(nh,ns,l,nr,ng,nb);if(nr!=r||ng!=g||nb!=b){dib[o]=nb;dib[o+1]=ng;dib[o+2]=nr;result.changed=true;++result.changedColors;result.changedPixels+=usage[i];}}}
    else {size_t bp=info.bitCount/8;for(size_t y=0;y<ah;++y){BYTE* row=&dib[info.pixelOffset+y*info.rowStride];for(LONG x=0;x<info.width;++x){BYTE* p=row+(size_t)x*bp;BYTE b=p[0],g=p[1],r=p[2];if(IsMagentaKey(r,g,b))continue;double h,s,l;RgbToHsl(r,g,b,h,s,l);if(s<.18||HueDistance(h,sh)>window)continue;double rel=WrapHue(h-sh);if(rel>180)rel-=360;BYTE nr,ng,nb;HslToRgb(WrapHue(th+rel),ts<.05?0:s,l,nr,ng,nb);if(nr!=r||ng!=g||nb!=b){p[0]=nb;p[1]=ng;p[2]=nr;result.changed=true;++result.changedPixels;}}}}
    if(!result.changed)result.note=L"Dominant hue found, but no used colors fell inside the protected hue window.";return true;
}

namespace
{
    static void PutU16(std::vector<BYTE>& d, size_t o, WORD v)
    {
        d[o] = (BYTE)(v & 255); d[o+1] = (BYTE)((v >> 8) & 255);
    }
    static void PutU32(std::vector<BYTE>& d, size_t o, DWORD v)
    {
        d[o] = (BYTE)(v & 255); d[o+1] = (BYTE)((v >> 8) & 255);
        d[o+2] = (BYTE)((v >> 16) & 255); d[o+3] = (BYTE)((v >> 24) & 255);
    }
    static bool WEqualsNoCase(const std::wstring& a, const wchar_t* b)
    {
        return _wcsicmp(a.c_str(), b) == 0;
    }
}


bool BitmapEngine::RecolorWholeBitmapPreserveFormat(std::vector<BYTE>& dib,
                                                     BYTE targetR, BYTE targetG, BYTE targetB,
                                                     RecolorResult& result,
                                                     std::wstring& errorText)
{
    result.changed=false;
    result.paletteEntries=0;
    result.usedPaletteEntries=0;
    result.changedColors=0;
    result.changedPixels=0;
    result.sourceHueDegrees=0;
    result.targetHueDegrees=0;
    result.note.clear();

    BitmapInfoSummary info;
    if(!ParseDib(dib,info,errorText))
        return false;

    double targetHue,targetSat,targetLight;
    RgbToHsl(targetR,targetG,targetB,targetHue,targetSat,targetLight);
    result.targetHueDegrees=targetHue;

    // Indexed Luna resources are the important case here. Preserve the palette
    // and indices exactly; only palette RGB values are adjusted. This avoids
    // converting transparency/mask-oriented sprite sheets to 24-bpp.
    if(info.bitCount<=8)
    {
        std::vector<unsigned long> usage(info.paletteEntries,0);
        std::vector<BYTE> indices;
        if(!DecodeIndices(dib,info,indices,errorText))
            return false;

        for(size_t i=0;i<indices.size();++i)
            if(indices[i]<usage.size())
                ++usage[indices[i]];

        double sumLight=0.0;
        unsigned long weighted=0;
        for(size_t i=0;i<info.paletteEntries;++i)
        {
            if(!usage[i]) continue;
            ++result.usedPaletteEntries;
            size_t o=info.headerSize+i*4;
            BYTE b=dib[o],g=dib[o+1],r=dib[o+2];
            if(IsMagentaKey(r,g,b)) continue;

            double sh,ss,sl;
            RgbToHsl(r,g,b,sh,ss,sl);
            if(ss<0.08 || sl<=0.012 || sl>=0.988) continue;
            sumLight+=sl*(double)usage[i];
            weighted+=usage[i];
        }
        result.paletteEntries=(unsigned int)info.paletteEntries;
        double sourceCenter=weighted?sumLight/(double)weighted:0.5;

        for(size_t i=0;i<info.paletteEntries;++i)
        {
            if(!usage[i]) continue;
            size_t o=info.headerSize+i*4;
            BYTE b=dib[o],g=dib[o+1],r=dib[o+2];
            if(IsMagentaKey(r,g,b)) continue;

            double sh,ss,sl;
            RgbToHsl(r,g,b,sh,ss,sl);
            if(ss<0.08 || sl<=0.012 || sl>=0.988) continue;

            double newLight=targetLight+(sl-sourceCenter)*0.55;
            double lo=targetLight-0.28, hi=targetLight+0.28;
            if(lo<0.0) lo=0.0; if(hi>1.0) hi=1.0;
            if(newLight<lo) newLight=lo;
            if(newLight>hi) newLight=hi;

            BYTE nr,ng,nb;
            HslToRgb(targetHue,targetSat,newLight,nr,ng,nb);
            if(nr!=r || ng!=g || nb!=b)
            {
                dib[o]=nb; dib[o+1]=ng; dib[o+2]=nr;
                ++result.changedColors;
                result.changedPixels+=usage[i];
                result.changed=true;
            }
        }

        if(!result.changed)
            result.note=L"No recolorable indexed palette colors were found.";
        return true;
    }

    // True-color resources: modify BGR in place and preserve any fourth byte
    // (alpha/reserved) exactly.
    if((info.bitCount!=24 && info.bitCount!=32) || info.compression!=BI_RGB)
    {
        result.note=L"Unsupported true-color format for format-preserving bulk recolor; left unchanged.";
        return true;
    }

    const size_t h=info.height<0?(size_t)-info.height:(size_t)info.height;
    const size_t bytesPerPixel=info.bitCount/8;

    double sumLight=0.0;
    unsigned long lightCount=0;
    for(size_t y=0;y<h;++y)
    {
        const BYTE* row=&dib[info.pixelOffset+y*info.rowStride];
        for(LONG x=0;x<info.width;++x)
        {
            const BYTE* p=row+(size_t)x*bytesPerPixel;
            BYTE b=p[0],g=p[1],r=p[2];
            if(IsMagentaKey(r,g,b)) continue;

            double sh,ss,sl;
            RgbToHsl(r,g,b,sh,ss,sl);
            if(ss<0.08 || sl<=0.012 || sl>=0.988) continue;
            sumLight+=sl;
            ++lightCount;
        }
    }
    double sourceCenter=lightCount?sumLight/(double)lightCount:0.5;

    std::set<DWORD> changed;
    for(size_t y=0;y<h;++y)
    {
        BYTE* row=&dib[info.pixelOffset+y*info.rowStride];
        for(LONG x=0;x<info.width;++x)
        {
            BYTE* p=row+(size_t)x*bytesPerPixel;
            BYTE b=p[0],g=p[1],r=p[2];
            if(IsMagentaKey(r,g,b)) continue;

            double sh,ss,sl;
            RgbToHsl(r,g,b,sh,ss,sl);
            if(ss<0.08 || sl<=0.012 || sl>=0.988) continue;

            double newLight=targetLight+(sl-sourceCenter)*0.55;
            double lo=targetLight-0.28, hi=targetLight+0.28;
            if(lo<0.0) lo=0.0; if(hi>1.0) hi=1.0;
            if(newLight<lo) newLight=lo;
            if(newLight>hi) newLight=hi;

            BYTE nr,ng,nb;
            HslToRgb(targetHue,targetSat,newLight,nr,ng,nb);
            if(nr!=r || ng!=g || nb!=b)
            {
                changed.insert(ColorKey(r,g,b));
                p[0]=nb; p[1]=ng; p[2]=nr;
                // p[3] is intentionally untouched for 32-bpp resources.
                ++result.changedPixels;
                result.changed=true;
            }
        }
    }
    result.changedColors=(unsigned int)changed.size();

    if(!result.changed)
        result.note=L"No recolorable true-color pixels were found.";
    return true;
}

bool BitmapEngine::ConvertTo24Bpp(std::vector<BYTE>& dib, std::wstring& errorText)
{
    BitmapInfoSummary info;
    if (!ParseDib(dib, info, errorText)) return false;
    if (info.bitCount == 24 && info.compression == BI_RGB) return true;

    std::vector<DWORD> colors;
    if (!GetPixels(dib, info, colors, errorText)) return false;

    const size_t w = (size_t)info.width;
    const size_t h = info.height < 0 ? (size_t)-info.height : (size_t)info.height;
    const size_t stride = ((w * 24 + 31) / 32) * 4;
    const size_t pixelBytes = stride * h;
    std::vector<BYTE> out(40 + pixelBytes, 0);
    PutU32(out, 0, 40);
    PutU32(out, 4, (DWORD)info.width);
    PutU32(out, 8, (DWORD)info.height);
    PutU16(out, 12, 1);
    PutU16(out, 14, 24);
    PutU32(out, 16, BI_RGB);
    PutU32(out, 20, (DWORD)pixelBytes);

    for (size_t y = 0; y < h; ++y)
    {
        BYTE* row = &out[40 + y * stride];
        for (size_t x = 0; x < w; ++x)
        {
            DWORD c = colors[y*w+x];
            row[x*3+0] = (BYTE)(c & 255);
            row[x*3+1] = (BYTE)((c >> 8) & 255);
            row[x*3+2] = (BYTE)((c >> 16) & 255);
        }
    }
    dib.swap(out);
    return true;
}

bool BitmapEngine::RecolorStateCell(std::vector<BYTE>& dib,
                                    unsigned int stateIndex,
                                    unsigned int imageCount,
                                    const std::wstring& imageLayout,
                                    BYTE targetR, BYTE targetG, BYTE targetB,
                                    double hueWindowDegrees,
                                    RecolorResult& result,
                                    std::wstring& errorText)
{
    result.changed = false; result.paletteEntries = result.usedPaletteEntries = 0;
    result.changedColors = 0; result.changedPixels = 0; result.sourceHueDegrees = 0;
    result.targetHueDegrees = 0; result.note.clear();
    if (imageCount == 0) imageCount = 1;
    if (stateIndex >= imageCount) { errorText = L"State index is outside ImageCount."; return false; }
    if (!ConvertTo24Bpp(dib, errorText)) return false;

    BitmapInfoSummary info;
    if (!ParseDib(dib, info, errorText)) return false;
    const size_t w = (size_t)info.width;
    const size_t h = info.height < 0 ? (size_t)-info.height : (size_t)info.height;
    bool horizontal = WEqualsNoCase(imageLayout, L"horizontal");
    bool vertical = WEqualsNoCase(imageLayout, L"vertical") || imageLayout.empty();
    if (!horizontal && !vertical) vertical = true;

    size_t x0=0, x1=w, y0=0, y1=h;
    if (horizontal)
    {
        if (w % imageCount != 0) { errorText=L"Bitmap width is not divisible by ImageCount."; return false; }
        size_t cw=w/imageCount; x0=cw*stateIndex; x1=x0+cw;
    }
    else
    {
        if (h % imageCount != 0) { errorText=L"Bitmap height is not divisible by ImageCount."; return false; }
        size_t ch=h/imageCount; y0=ch*stateIndex; y1=y0+ch;
    }

    HueHistogram hist;
    unsigned long candidates=0;
    for (size_t ly=y0; ly<y1; ++ly)
    {
        size_t py = info.height > 0 ? (h - 1 - ly) : ly;
        BYTE* row=&dib[info.pixelOffset + py*info.rowStride];
        for (size_t x=x0; x<x1; ++x)
        {
            BYTE b=row[x*3], g=row[x*3+1], r=row[x*3+2];
            if (IsMagentaKey(r,g,b)) continue;
            double hh,ss,ll; RgbToHsl(r,g,b,hh,ss,ll);
            if (ss >= .16 && ll > .02 && ll < .98) { hist.Add(r,g,b,1.0); ++candidates; }
        }
    }
    double sourceHue=0;
    if (!hist.Dominant(sourceHue))
    {
        result.note=L"Selected state cell contains no dominant saturated hue; left unchanged.";
        return true;
    }
    double th,ts,tl; RgbToHsl(targetR,targetG,targetB,th,ts,tl);
    double delta=th-sourceHue; while(delta>180)delta-=360; while(delta<-180)delta+=360;
    result.sourceHueDegrees=sourceHue; result.targetHueDegrees=th;

    std::set<DWORD> changedColors;
    for (size_t ly=y0; ly<y1; ++ly)
    {
        size_t py = info.height > 0 ? (h - 1 - ly) : ly;
        BYTE* row=&dib[info.pixelOffset + py*info.rowStride];
        for (size_t x=x0; x<x1; ++x)
        {
            BYTE& b=row[x*3]; BYTE& g=row[x*3+1]; BYTE& r=row[x*3+2];
            if (IsMagentaKey(r,g,b)) continue;
            double hh,ss,ll; RgbToHsl(r,g,b,hh,ss,ll);
            if (ss < .12 || ll <= .015 || ll >= .985 || HueDistance(hh,sourceHue) > hueWindowDegrees) continue;
            BYTE nr,ng,nb; HslToRgb(WrapHue(hh+delta),ss,ll,nr,ng,nb);
            if (nr!=r || ng!=g || nb!=b)
            {
                changedColors.insert(ColorKey(r,g,b)); r=nr;g=ng;b=nb; ++result.changedPixels;
            }
        }
    }
    result.changedColors=(unsigned int)changedColors.size();
    result.changed=result.changedPixels!=0;
    if (!result.changed) result.note=L"Dominant hue was found, but no pixels fell inside the recolor window.";
    (void)candidates;
    return true;
}


bool BitmapEngine::RecolorWholeStateCell(std::vector<BYTE>& dib,
                                         unsigned int stateIndex,
                                         unsigned int imageCount,
                                         const std::wstring& imageLayout,
                                         BYTE targetR, BYTE targetG, BYTE targetB,
                                         RecolorResult& result,
                                         std::wstring& errorText)
{
    result.changed=false;
    result.paletteEntries=result.usedPaletteEntries=0;
    result.changedColors=0;
    result.changedPixels=0;
    result.sourceHueDegrees=0;
    result.targetHueDegrees=0;
    result.note.clear();

    if(imageCount==0) imageCount=1;
    if(stateIndex>=imageCount){errorText=L"State index is outside ImageCount.";return false;}
    if(!ConvertTo24Bpp(dib,errorText)) return false;

    BitmapInfoSummary info;
    if(!ParseDib(dib,info,errorText)) return false;

    const size_t w=(size_t)info.width;
    const size_t h=info.height<0?(size_t)-info.height:(size_t)info.height;
    bool horizontal=WEqualsNoCase(imageLayout,L"horizontal");
    bool vertical=WEqualsNoCase(imageLayout,L"vertical") || imageLayout.empty();
    if(!horizontal && !vertical) vertical=true;

    size_t x0=0,x1=w,y0=0,y1=h;
    if(horizontal)
    {
        if(w%imageCount!=0){errorText=L"Bitmap width is not divisible by ImageCount.";return false;}
        size_t cw=w/imageCount; x0=cw*stateIndex; x1=x0+cw;
    }
    else
    {
        if(h%imageCount!=0){errorText=L"Bitmap height is not divisible by ImageCount.";return false;}
        size_t ch=h/imageCount; y0=ch*stateIndex; y1=y0+ch;
    }

    double targetHue,targetSat,targetLight;
    RgbToHsl(targetR,targetG,targetB,targetHue,targetSat,targetLight);
    result.targetHueDegrees=targetHue;

    // First measure the state-cell's average colored-pixel lightness.
    // The old algorithm preserved absolute source lightness, which could push
    // a dark teal target into very bright aqua/cyan highlights. v3.7 instead
    // preserves RELATIVE shading around the exact chosen target color.
    double sumLight=0.0;
    unsigned long lightCount=0;
    for(size_t ly=y0;ly<y1;++ly)
    {
        size_t py=info.height>0?(h-1-ly):ly;
        BYTE* row=&dib[info.pixelOffset+py*info.rowStride];
        for(size_t x=x0;x<x1;++x)
        {
            BYTE b=row[x*3], g=row[x*3+1], r=row[x*3+2];
            if(IsMagentaKey(r,g,b)) continue;
            double sh,ss,sl; RgbToHsl(r,g,b,sh,ss,sl);
            if(ss<0.08 || sl<=0.012 || sl>=0.988) continue;
            sumLight+=sl; ++lightCount;
        }
    }
    double sourceCenter=lightCount?sumLight/(double)lightCount:0.5;

    std::set<DWORD> changedColors;
    for(size_t ly=y0;ly<y1;++ly)
    {
        size_t py=info.height>0?(h-1-ly):ly;
        BYTE* row=&dib[info.pixelOffset+py*info.rowStride];

        for(size_t x=x0;x<x1;++x)
        {
            BYTE& b=row[x*3];
            BYTE& g=row[x*3+1];
            BYTE& r=row[x*3+2];

            if(IsMagentaKey(r,g,b)) continue;

            double sh,ss,sl;
            RgbToHsl(r,g,b,sh,ss,sl);
            if(ss<0.08 || sl<=0.012 || sl>=0.988) continue;

            // Keep the target hue/saturation exact. Preserve only the source's
            // relative light/dark relief, centered around the selected color.
            double newLight=targetLight+(sl-sourceCenter)*0.55;
            double lo=targetLight-0.28, hi=targetLight+0.28;
            if(lo<0.0) lo=0.0; if(hi>1.0) hi=1.0;
            if(newLight<lo) newLight=lo;
            if(newLight>hi) newLight=hi;
            if(newLight<0.0) newLight=0.0;
            if(newLight>1.0) newLight=1.0;

            BYTE nr,ng,nb;
            HslToRgb(targetHue,targetSat,newLight,nr,ng,nb);

            if(nr!=r || ng!=g || nb!=b)
            {
                changedColors.insert(ColorKey(r,g,b));
                r=nr; g=ng; b=nb;
                ++result.changedPixels;
            }
        }
    }

    result.changedColors=(unsigned int)changedColors.size();
    result.changed=result.changedPixels!=0;
    if(!result.changed) result.note=L"Selected state contains no recolorable chromatic pixels.";
    return true;
}



bool BitmapEngine::RecolorSurfaceStateCell(std::vector<BYTE>& dib,
                                           unsigned int stateIndex,
                                           unsigned int imageCount,
                                           const std::wstring& imageLayout,
                                           BYTE targetR, BYTE targetG, BYTE targetB,
                                           RecolorResult& result,
                                           std::wstring& errorText)
{
    result.changed=false;
    result.paletteEntries=result.usedPaletteEntries=0;
    result.changedColors=0;
    result.changedPixels=0;
    result.sourceHueDegrees=0;
    result.targetHueDegrees=0;
    result.note.clear();

    if(imageCount==0) imageCount=1;
    if(stateIndex>=imageCount){errorText=L"State index is outside ImageCount.";return false;}
    if(!ConvertTo24Bpp(dib,errorText)) return false;

    BitmapInfoSummary info;
    if(!ParseDib(dib,info,errorText)) return false;

    const size_t w=(size_t)info.width;
    const size_t h=info.height<0?(size_t)-info.height:(size_t)info.height;
    bool horizontal=WEqualsNoCase(imageLayout,L"horizontal");

    size_t x0=0,x1=w,y0=0,y1=h;
    if(horizontal)
    {
        if(w%imageCount!=0){errorText=L"Bitmap width is not divisible by ImageCount.";return false;}
        size_t cw=w/imageCount; x0=cw*stateIndex; x1=x0+cw;
    }
    else
    {
        if(h%imageCount!=0){errorText=L"Bitmap height is not divisible by ImageCount.";return false;}
        size_t ch=h/imageCount; y0=ch*stateIndex; y1=y0+ch;
    }

    double targetHue,targetSat,targetLight;
    RgbToHsl(targetR,targetG,targetB,targetHue,targetSat,targetLight);
    result.targetHueDegrees=targetHue;

    // Measure the whole visible surface, including neutral/white pixels.
    double sumLight=0.0;
    unsigned long lightCount=0;
    for(size_t ly=y0;ly<y1;++ly)
    {
        size_t py=info.height>0?(h-1-ly):ly;
        BYTE* row=&dib[info.pixelOffset+py*info.rowStride];
        for(size_t x=x0;x<x1;++x)
        {
            BYTE b=row[x*3],g=row[x*3+1],r=row[x*3+2];
            if(IsMagentaKey(r,g,b)) continue;
            double hh,ss,ll;
            RgbToHsl(r,g,b,hh,ss,ll);
            sumLight+=ll;
            ++lightCount;
        }
    }
    double sourceCenter=lightCount?sumLight/(double)lightCount:0.5;

    std::set<DWORD> changedColors;
    for(size_t ly=y0;ly<y1;++ly)
    {
        size_t py=info.height>0?(h-1-ly):ly;
        BYTE* row=&dib[info.pixelOffset+py*info.rowStride];

        for(size_t x=x0;x<x1;++x)
        {
            BYTE& b=row[x*3];
            BYTE& g=row[x*3+1];
            BYTE& r=row[x*3+2];

            if(IsMagentaKey(r,g,b)) continue;

            double sh,ss,sl;
            RgbToHsl(r,g,b,sh,ss,sl);

            // Keep relief but compress it around the requested dark surface.
            // Pure white therefore becomes a lighter shade of the dark target
            // instead of remaining an unmodified white rectangle.
            double newLight=targetLight+(sl-sourceCenter)*0.32;
            double lo=targetLight-0.20, hi=targetLight+0.22;
            if(lo<0.0) lo=0.0;
            if(hi>1.0) hi=1.0;
            if(newLight<lo) newLight=lo;
            if(newLight>hi) newLight=hi;

            BYTE nr,ng,nb;
            HslToRgb(targetHue,targetSat,newLight,nr,ng,nb);

            if(nr!=r || ng!=g || nb!=b)
            {
                changedColors.insert(ColorKey(r,g,b));
                r=nr;g=ng;b=nb;
                ++result.changedPixels;
            }
        }
    }

    result.changedColors=(unsigned int)changedColors.size();
    result.changed=result.changedPixels!=0;
    if(!result.changed) result.note=L"Selected surface contains no recolorable pixels.";
    return true;
}


bool BitmapEngine::RecolorTintedStateCell(std::vector<BYTE>& dib,
                                          unsigned int stateIndex,
                                          unsigned int imageCount,
                                          const std::wstring& imageLayout,
                                          BYTE targetR, BYTE targetG, BYTE targetB,
                                          RecolorResult& result,
                                          std::wstring& errorText)
{
    result.changed=false;
    result.paletteEntries=result.usedPaletteEntries=0;
    result.changedColors=0;
    result.changedPixels=0;
    result.sourceHueDegrees=0;
    result.targetHueDegrees=0;
    result.note.clear();

    if(imageCount==0) imageCount=1;
    if(stateIndex>=imageCount){errorText=L"State index is outside ImageCount.";return false;}
    if(!ConvertTo24Bpp(dib,errorText)) return false;

    BitmapInfoSummary info;
    if(!ParseDib(dib,info,errorText)) return false;

    const size_t w=(size_t)info.width;
    const size_t h=info.height<0?(size_t)-info.height:(size_t)info.height;
    bool horizontal=WEqualsNoCase(imageLayout,L"horizontal");
    bool vertical=WEqualsNoCase(imageLayout,L"vertical") || imageLayout.empty();
    if(!horizontal && !vertical) vertical=true;

    size_t x0=0,x1=w,y0=0,y1=h;
    if(horizontal)
    {
        if(w%imageCount!=0){errorText=L"Bitmap width is not divisible by ImageCount.";return false;}
        size_t cw=w/imageCount; x0=cw*stateIndex; x1=x0+cw;
    }
    else
    {
        if(h%imageCount!=0){errorText=L"Bitmap height is not divisible by ImageCount.";return false;}
        size_t ch=h/imageCount; y0=ch*stateIndex; y1=y0+ch;
    }

    double targetHue,targetSat,targetLight;
    RgbToHsl(targetR,targetG,targetB,targetHue,targetSat,targetLight);
    result.targetHueDegrees=targetHue;

    double sumLight=0.0;
    unsigned long lightCount=0;
    for(size_t ly=y0;ly<y1;++ly)
    {
        size_t py=info.height>0?(h-1-ly):ly;
        BYTE* row=&dib[info.pixelOffset+py*info.rowStride];
        for(size_t x=x0;x<x1;++x)
        {
            BYTE b=row[x*3], g=row[x*3+1], r=row[x*3+2];
            if(IsMagentaKey(r,g,b)) continue;
            double sh,ss,sl; RgbToHsl(r,g,b,sh,ss,sl);
            if(sl<=0.006 || sl>=0.997) continue;
            sumLight+=sl; ++lightCount;
        }
    }
    double sourceCenter=lightCount?sumLight/(double)lightCount:0.5;

    std::set<DWORD> changedColors;
    for(size_t ly=y0;ly<y1;++ly)
    {
        size_t py=info.height>0?(h-1-ly):ly;
        BYTE* row=&dib[info.pixelOffset+py*info.rowStride];

        for(size_t x=x0;x<x1;++x)
        {
            BYTE& b=row[x*3]; BYTE& g=row[x*3+1]; BYTE& r=row[x*3+2];
            if(IsMagentaKey(r,g,b)) continue;

            double sh,ss,sl; RgbToHsl(r,g,b,sh,ss,sl);
            if(sl<=0.006 || sl>=0.997) continue;

            double newLight=targetLight+(sl-sourceCenter)*0.55;
            double lo=targetLight-0.30, hi=targetLight+0.30;
            if(lo<0.0) lo=0.0; if(hi>1.0) hi=1.0;
            if(newLight<lo) newLight=lo;
            if(newLight>hi) newLight=hi;

            BYTE nr,ng,nb;
            HslToRgb(targetHue,targetSat,newLight,nr,ng,nb);

            if(nr!=r || ng!=g || nb!=b)
            {
                changedColors.insert(ColorKey(r,g,b));
                r=nr; g=ng; b=nb;
                ++result.changedPixels;
            }
        }
    }

    result.changedColors=(unsigned int)changedColors.size();
    result.changed=result.changedPixels!=0;
    if(!result.changed) result.note=L"Selected state contains no recolorable pixels.";
    return true;
}


bool BitmapEngine::ExtractStateCell(const std::vector<BYTE>& sourceDib,
                                    unsigned int stateIndex,
                                    unsigned int imageCount,
                                    const std::wstring& imageLayout,
                                    std::vector<BYTE>& cellDib,
                                    std::wstring& errorText)
{
    errorText.clear();
    cellDib=sourceDib;
    if(!ConvertTo24Bpp(cellDib,errorText)) return false;

    BitmapInfoSummary si;
    if(!ParseDib(cellDib,si,errorText)) return false;
    if(imageCount==0) imageCount=1;
    if(stateIndex>=imageCount){errorText=L"State index is outside ImageCount.";return false;}

    LONG fullW=si.width;
    LONG fullH=si.height<0?-si.height:si.height;
    bool horizontal=_wcsicmp(imageLayout.c_str(),L"horizontal")==0;
    LONG cw=fullW, ch=fullH;
    if(horizontal)
    {
        if(fullW%(LONG)imageCount){errorText=L"Bitmap width is not divisible by ImageCount.";return false;}
        cw=fullW/(LONG)imageCount;
    }
    else
    {
        if(fullH%(LONG)imageCount){errorText=L"Bitmap height is not divisible by ImageCount.";return false;}
        ch=fullH/(LONG)imageCount;
    }

    const size_t row=((size_t)cw*3u+3u)&~3u;
    const size_t hdr=sizeof(BITMAPINFOHEADER);
    std::vector<BYTE> out(hdr+row*(size_t)ch,0);
    BITMAPINFOHEADER* bh=(BITMAPINFOHEADER*)&out[0];
    bh->biSize=sizeof(BITMAPINFOHEADER);
    bh->biWidth=cw;
    bh->biHeight=ch; // normal bottom-up standalone BMP
    bh->biPlanes=1;
    bh->biBitCount=24;
    bh->biCompression=BI_RGB;
    bh->biSizeImage=(DWORD)(row*(size_t)ch);

    // Copy in logical top-to-bottom coordinates, converting to bottom-up output.
    for(LONG ly=0;ly<ch;++ly)
    {
        LONG srcLogicalY=horizontal ? ly : ((LONG)stateIndex*ch+ly);
        LONG srcPhysicalY=si.height>0 ? (fullH-1-srcLogicalY) : srcLogicalY;
        LONG dstPhysicalY=ch-1-ly;
        LONG srcX=horizontal ? ((LONG)stateIndex*cw) : 0;

        const BYTE* s=&cellDib[si.pixelOffset+(size_t)srcPhysicalY*si.rowStride+(size_t)srcX*3u];
        BYTE* d=&out[hdr+(size_t)dstPhysicalY*row];
        memcpy(d,s,(size_t)cw*3u);
    }

    cellDib.swap(out);
    return true;
}

bool BitmapEngine::ReplaceStateCell(std::vector<BYTE>& destinationDib,
                                    unsigned int stateIndex,
                                    unsigned int imageCount,
                                    const std::wstring& imageLayout,
                                    const std::vector<BYTE>& inputCellDib,
                                    std::wstring& errorText)
{
    errorText.clear();
    if(!ConvertTo24Bpp(destinationDib,errorText)) return false;

    std::vector<BYTE> cell=inputCellDib;
    if(!ConvertTo24Bpp(cell,errorText)) return false;

    BitmapInfoSummary di,ci;
    if(!ParseDib(destinationDib,di,errorText)) return false;
    if(!ParseDib(cell,ci,errorText)) return false;

    if(imageCount==0) imageCount=1;
    if(stateIndex>=imageCount){errorText=L"State index is outside ImageCount.";return false;}

    LONG fullW=di.width;
    LONG fullH=di.height<0?-di.height:di.height;
    bool horizontal=_wcsicmp(imageLayout.c_str(),L"horizontal")==0;
    LONG cw=fullW, ch=fullH;
    if(horizontal)
    {
        if(fullW%(LONG)imageCount){errorText=L"Bitmap width is not divisible by ImageCount.";return false;}
        cw=fullW/(LONG)imageCount;
    }
    else
    {
        if(fullH%(LONG)imageCount){errorText=L"Bitmap height is not divisible by ImageCount.";return false;}
        ch=fullH/(LONG)imageCount;
    }

    LONG cellH=ci.height<0?-ci.height:ci.height;
    if(ci.width!=cw || cellH!=ch)
    {
        std::wstringstream s;
        s<<L"Replacement state dimensions must be "<<cw<<L" x "<<ch
         <<L".\r\nLoaded: "<<ci.width<<L" x "<<cellH;
        errorText=s.str();
        return false;
    }

    for(LONG ly=0;ly<ch;++ly)
    {
        LONG dstLogicalY=horizontal ? ly : ((LONG)stateIndex*ch+ly);
        LONG dstPhysicalY=di.height>0 ? (fullH-1-dstLogicalY) : dstLogicalY;
        LONG srcPhysicalY=ci.height>0 ? (cellH-1-ly) : ly;
        LONG dstX=horizontal ? ((LONG)stateIndex*cw) : 0;

        BYTE* d=&destinationDib[di.pixelOffset+(size_t)dstPhysicalY*di.rowStride+(size_t)dstX*3u];
        const BYTE* s=&cell[ci.pixelOffset+(size_t)srcPhysicalY*ci.rowStride];
        memcpy(d,s,(size_t)cw*3u);
    }
    return true;
}


static BYTE LsBlend(BYTE a,BYTE b,double t)
{
    double v=(double)a+((double)b-(double)a)*t;
    if(v<0) v=0; if(v>255) v=255;
    return (BYTE)(v+0.5);
}


static bool LsGradientState(std::vector<BYTE>& dib,
                            unsigned int stateIndex,unsigned int imageCount,
                            const std::wstring& imageLayout,
                            unsigned int leftMargin,unsigned int rightMargin,
                            BYTE r1,BYTE g1,BYTE b1,BYTE r2,BYTE g2,BYTE b2,
                            bool strongTint,RecolorResult& result,std::wstring& errorText)
{
    result.changed=false; result.paletteEntries=result.usedPaletteEntries=0;
    result.changedColors=0; result.changedPixels=0; result.sourceHueDegrees=0;
    result.targetHueDegrees=0; result.note.clear();

    if(imageCount==0) imageCount=1;
    if(stateIndex>=imageCount){errorText=L"State index is outside ImageCount.";return false;}
    if(!BitmapEngine::ConvertTo24Bpp(dib,errorText)) return false;

    BitmapInfoSummary info;
    if(!BitmapEngine::ParseDib(dib,info,errorText)) return false;
    size_t w=(size_t)info.width;
    size_t h=info.height<0?(size_t)-info.height:(size_t)info.height;
    bool horizontal=_wcsicmp(imageLayout.c_str(),L"horizontal")==0;
    size_t x0=0,x1=w,y0=0,y1=h;
    if(horizontal)
    {
        if(w%imageCount){errorText=L"Bitmap width is not divisible by ImageCount.";return false;}
        size_t cw=w/imageCount; x0=cw*stateIndex; x1=x0+cw;
    }
    else
    {
        if(h%imageCount){errorText=L"Bitmap height is not divisible by ImageCount.";return false;}
        size_t ch=h/imageCount; y0=ch*stateIndex; y1=y0+ch;
    }

    double sum=0.0; unsigned long count=0;
    for(size_t ly=y0;ly<y1;++ly)
    {
        size_t py=info.height>0?(h-1-ly):ly;
        BYTE* row=&dib[info.pixelOffset+py*info.rowStride];
        for(size_t x=x0;x<x1;++x)
        {
            BYTE b=row[x*3],g=row[x*3+1],r=row[x*3+2];
            if(r>=248 && b>=248 && g<=8) continue;
            double hh,ss,ll; RgbToHsl(r,g,b,hh,ss,ll);
            if(strongTint){ if(ll<=0.006||ll>=0.997) continue; }
            else { if(ss<0.08||ll<=0.012||ll>=0.988) continue; }
            sum+=ll; ++count;
        }
    }
    double center=count?sum/(double)count:0.5;

    std::set<DWORD> changed;
    size_t cw=x1-x0;
    for(size_t ly=y0;ly<y1;++ly)
    {
        size_t py=info.height>0?(h-1-ly):ly;
        BYTE* row=&dib[info.pixelOffset+py*info.rowStride];
        for(size_t x=x0;x<x1;++x)
        {
            BYTE& b=row[x*3]; BYTE& g=row[x*3+1]; BYTE& r=row[x*3+2];
            if(r>=248 && b>=248 && g<=8) continue;

            double oh,os,ol; RgbToHsl(r,g,b,oh,os,ol);
            if(strongTint){ if(ol<=0.006||ol>=0.997) continue; }
            else { if(os<0.08||ol<=0.012||ol>=0.988) continue; }

            size_t localX=x-x0;
            double t=0.0;
            if(cw>1)
            {
                // XP's stretch renderer keeps the left/right SizingMargins at
                // fixed width and stretches only the center. Put the FULL
                // gradient in that stretchable center, otherwise a wide
                // titlebar only shows a small middle slice of the requested ramp.
                if(leftMargin+rightMargin+1<cw)
                {
                    size_t centerStart=(size_t)leftMargin;
                    size_t centerEnd=cw-(size_t)rightMargin-1;
                    if(localX<=centerStart) t=0.0;
                    else if(localX>=centerEnd) t=1.0;
                    else t=(double)(localX-centerStart)/(double)(centerEnd-centerStart);
                }
                else
                    t=(double)localX/(double)(cw-1);
            }
            BYTE tr=LsBlend(r1,r2,t),tg=LsBlend(g1,g2,t),tb=LsBlend(b1,b2,t);
            double th,ts,tl; RgbToHsl(tr,tg,tb,th,ts,tl);
            if(strongTint && ts<0.35) ts=0.35;
            double nl=tl+(ol-center)*0.22;
            double lo=tl-0.18, hi=tl+0.18;
            if(lo<0) lo=0; if(hi>1) hi=1;
            if(nl<lo) nl=lo; if(nl>hi) nl=hi;

            BYTE nr,ng,nb; HslToRgb(th,ts,nl,nr,ng,nb);
            if(nr!=r||ng!=g||nb!=b)
            {
                changed.insert(ColorKey(r,g,b));
                r=nr;g=ng;b=nb;++result.changedPixels;
            }
        }
    }
    result.changedColors=(unsigned int)changed.size();
    result.changed=result.changedPixels!=0;
    return true;
}

bool BitmapEngine::RecolorWholeStateCellGradient(std::vector<BYTE>& dib,
                                                 unsigned int stateIndex,unsigned int imageCount,
                                                 const std::wstring& imageLayout,
                                                 unsigned int leftMargin,unsigned int rightMargin,
                                                 BYTE r1,BYTE g1,BYTE b1,BYTE r2,BYTE g2,BYTE b2,
                                                 RecolorResult& result,std::wstring& errorText)
{
    return LsGradientState(dib,stateIndex,imageCount,imageLayout,leftMargin,rightMargin,
                           r1,g1,b1,r2,g2,b2,false,result,errorText);
}

bool BitmapEngine::RecolorTintedStateCellGradient(std::vector<BYTE>& dib,
                                                  unsigned int stateIndex,unsigned int imageCount,
                                                  const std::wstring& imageLayout,
                                                  unsigned int leftMargin,unsigned int rightMargin,
                                                  BYTE r1,BYTE g1,BYTE b1,BYTE r2,BYTE g2,BYTE b2,
                                                  RecolorResult& result,std::wstring& errorText)
{
    return LsGradientState(dib,stateIndex,imageCount,imageLayout,leftMargin,rightMargin,
                           r1,g1,b1,r2,g2,b2,true,result,errorText);
}


bool BitmapEngine::ExpandStretchWidth(std::vector<BYTE>& dib,
                                      unsigned int imageCount,
                                      const std::wstring& imageLayout,
                                      unsigned int leftMargin,
                                      unsigned int rightMargin,
                                      unsigned int targetCellWidth,
                                      std::wstring& errorText)
{
    if(imageCount==0) imageCount=1;
    if(!ConvertTo24Bpp(dib,errorText)) return false;

    BitmapInfoSummary info;
    if(!ParseDib(dib,info,errorText)) return false;

    LONG fullW=info.width;
    LONG fullH=info.height<0?-info.height:info.height;
    bool horizontal=_wcsicmp(imageLayout.c_str(),L"horizontal")==0;

    LONG oldCellW=horizontal ? fullW/(LONG)imageCount : fullW;
    if(horizontal && fullW%(LONG)imageCount)
    {
        errorText=L"Bitmap width is not divisible by ImageCount.";
        return false;
    }

    if(oldCellW<=0 || (unsigned int)oldCellW>=targetCellWidth)
        return true;

    LONG newCellW=(LONG)targetCellWidth;
    LONG newFullW=horizontal ? newCellW*(LONG)imageCount : newCellW;

    size_t newStride=((size_t)newFullW*3u+3u)&~3u;
    size_t newPixelBytes=newStride*(size_t)fullH;
    std::vector<BYTE> out(sizeof(BITMAPINFOHEADER)+newPixelBytes,0);

    BITMAPINFOHEADER* bh=(BITMAPINFOHEADER*)&out[0];
    bh->biSize=sizeof(BITMAPINFOHEADER);
    bh->biWidth=newFullW;
    bh->biHeight=info.height;
    bh->biPlanes=1;
    bh->biBitCount=24;
    bh->biCompression=BI_RGB;
    bh->biSizeImage=(DWORD)newPixelBytes;

    unsigned int lm=leftMargin;
    unsigned int rm=rightMargin;
    if(lm+rm+2u>(unsigned int)oldCellW)
        lm=rm=0;

    LONG oldCenterStart=(LONG)lm;
    LONG oldCenterEnd=oldCellW-(LONG)rm-1;
    LONG newCenterStart=(LONG)lm;
    LONG newCenterEnd=newCellW-(LONG)rm-1;

    unsigned int states=horizontal?imageCount:1u;

    for(LONG py=0;py<fullH;++py)
    {
        const BYTE* srcRow=&dib[info.pixelOffset+(size_t)py*info.rowStride];
        BYTE* dstRow=&out[sizeof(BITMAPINFOHEADER)+(size_t)py*newStride];

        for(unsigned int state=0;state<states;++state)
        {
            LONG srcBase=horizontal?(LONG)state*oldCellW:0;
            LONG dstBase=horizontal?(LONG)state*newCellW:0;

            for(LONG x=0;x<newCellW;++x)
            {
                LONG sx=0;

                if(lm||rm)
                {
                    if(x<newCenterStart)
                        sx=x;
                    else if(x>newCenterEnd)
                        sx=oldCellW-(newCellW-x);
                    else if(newCenterEnd<=newCenterStart || oldCenterEnd<=oldCenterStart)
                        sx=oldCenterStart;
                    else
                    {
                        double t=(double)(x-newCenterStart)/(double)(newCenterEnd-newCenterStart);
                        sx=oldCenterStart+(LONG)((oldCenterEnd-oldCenterStart)*t+0.5);
                    }
                }
                else
                {
                    sx=newCellW>1 ?
                        (LONG)((double)x*(double)(oldCellW-1)/(double)(newCellW-1)+0.5) : 0;
                }

                if(sx<0) sx=0;
                if(sx>=oldCellW) sx=oldCellW-1;

                const BYTE* sp=srcRow+(size_t)(srcBase+sx)*3u;
                BYTE* dp=dstRow+(size_t)(dstBase+x)*3u;
                dp[0]=sp[0];
                dp[1]=sp[1];
                dp[2]=sp[2];
            }
        }
    }

    dib.swap(out);
    return true;
}
