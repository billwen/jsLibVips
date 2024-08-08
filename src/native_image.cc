#include <iostream>
#include "utils.h"
#include "native_image.h"

using namespace vips;

NativeImage::NativeImage(const Napi::CallbackInfo& info): Napi::ObjectWrap<NativeImage>(info) {
    mode_ = ImageMode::IMAGE;
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() == 0) {
        Napi::TypeError::New(env, "Missing arguments").ThrowAsJavaScriptException();
    }

    this->mode_ = ImageMode::IMAGE;
    // First argument is the path to the image file or configuration file
    if (info[0].IsString()) {
        std::string path = info[0].As<Napi::String>().Utf8Value();
        this->image_ = VImage::new_from_file(path.c_str(), VImage::option ()->set ("access", VIPS_ACCESS_SEQUENTIAL));
    } else if (info[0].IsObject()) {
//        std::cout << "NativeImage object" << std::endl;
        Napi::Object options = info[0].As<Napi::Object>();
        CreationOptions opts = parseCreationOptions(options);
        this->image_ = createRGBImage(opts);
        if (info.Length() >= 2) {
            // Extra arguments are available
            if (!info[1].IsNumber()) {
                Napi::TypeError::New(env, "Invalid mode").ThrowAsJavaScriptException();
            }

            // create the template
            if (info[1].As<Napi::Number>().Int32Value() == 1) {
                this->mode_ = ImageMode::COUNTDOWN;

                // Parse the countdown options
                this->countdownOptions_ = parseCountdownOptions(options);

                // Parpare the template
                initCountdownAnimation();

            } else {
                Napi::TypeError::New(env, "Invalid mode").ThrowAsJavaScriptException();
            }
        }
    } else {
        Napi::TypeError::New(env, "Invalid the first argument.").ThrowAsJavaScriptException();
    }
}

void NativeImage::initCountdownAnimation() {
    // 1. Create an empty image with background color - allready done!

    std::vector<VImage> labels;
    std::vector<int> modes = {VIPS_BLEND_MODE_OVER};
    std::vector<int> xLabel;
    std::vector<int> yLabel;

    // 2. Added background images
    labels.push_back(this->image_);

    // 3. Create labels images
    for (const auto& [key, value]: this->countdownOptions_.labels) {
        ColoredTextOptions labelOpts;
        labelOpts.textColor = hexadecimalColorToARGB(value.color);
        labelOpts.font = value.font;
        labelOpts.fontFile = value.fontFile;
        labelOpts.width = value.position.width;
        labelOpts.height = value.position.height;
        labelOpts.textAlignment = value.textAlignment;
        labelOpts.paddingTop = value.paddingTop;
        labelOpts.paddingBottom = value.paddingBottom;
        
        VImage labelImage = coloredTextImage(value.text, labelOpts);
        labels.push_back(labelImage);
        xLabel.push_back(value.position.x);
        yLabel.push_back(value.position.y);
    }

    // 3. draw the template
    this->image_ = VImage::composite(labels, modes, VImage::option()->set("x", xLabel)->set("y", yLabel));

    // 4. Initialize the digits images
    ColoredTextOptions digitOptions;
    digitOptions.textColor = hexadecimalColorToARGB(this->countdownOptions_.digits.style.color);
    digitOptions.width = this->countdownOptions_.digits.style.width;
    digitOptions.height = this->countdownOptions_.digits.style.height;
    digitOptions.font = this->countdownOptions_.digits.style.font;
    digitOptions.fontFile = this->countdownOptions_.digits.style.fontFile;

    for ( int i = 0; i < 100; i++) {
        std::string digitalText = jsvips::format("%02d", i);
        if (this->countdownOptions_.digits.textTemplate.size() > 0) {
            digitalText = jsvips::format(this->countdownOptions_.digits.textTemplate, digitalText.c_str());
        }

        VImage digit = coloredTextImage(digitalText, digitOptions);
        this->countdownDigits_.push_back(digit);
    }

}

Napi::Object NativeImage::Init(Napi::Env env, Napi::Object exports) {
    Napi::HandleScope scope(env);

    Napi::Function func = DefineClass(env, "NativeImage", {
        InstanceMethod<&NativeImage::DrawText>("drawText", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
        InstanceMethod<&NativeImage::Save>("save", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
        InstanceMethod<&NativeImage::RenderCountdownAnimation>("renderCountdownAnimation", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
        StaticMethod<&NativeImage::Text>("text", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
        StaticMethod<&NativeImage::CreateSRGBImage>("createSRGBImage", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
        StaticMethod<&NativeImage::CreateCountdownAnimation>("createCountdownAnimation", static_cast<napi_property_attributes>(napi_writable | napi_configurable)),
    });

    // Create a persistent reference to the class constructor. This will allow
    // a function called on a class prototype and a function called on a class
    // instance to be distinguished from each other.
    Napi::FunctionReference* constructor = new Napi::FunctionReference();
    *constructor = Napi::Persistent(func);
    constructor->SuppressDestruct();

    exports.Set("NativeImage", func);

    // Store the constructor as the add-on instance data. This will allow this
    // add-on to support multiple instances of itself running on multiple worker
    // threads, as well as multiple instances of itself running in different
    // contexts on the same thread.

    // By default, the value set on the environment here will be destroyed when
    // the add-on is unloaded using the `delete` operator, but it is also
    // possible to supply a custom deleter
    env.SetInstanceData<Napi::FunctionReference>(constructor);

    return exports;
}

//
// Create an empty image with background color
// JS: NativeImage.createImage(width, height, bgColor?)
// bgColor: HTML default to #FFFFFF
//
Napi::Value NativeImage::CreateSRGBImage(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);

    if (info.Length() == 0) {
        Napi::TypeError::New(env, "Missing CreationOptions").ThrowAsJavaScriptException();
    }

    // Retrieve the instance data we stored during `Init()`. We only stored the
    // constructor there, so we retrieve it here to create a new instance of the
    // JS class the constructor represents.
    Napi::FunctionReference* constructor = env.GetInstanceData<Napi::FunctionReference>();
    Napi::Object obj = constructor->New({info[0]});
    return scope.Escape(napi_value(obj)).ToObject();
}

Napi::Value NativeImage::Text(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);

    // Retrieve the instance data we stored during `Init()`. We only stored the
    // constructor there, so we retrieve it here to create a new instance of the
    // JS class the constructor represents.
    Napi::FunctionReference* constructor = env.GetInstanceData<Napi::FunctionReference>();
    Napi::Object obj = constructor->New({});
    return scope.Escape(napi_value(obj)).ToObject();
}

/**
 * Draw text on the image
 */
Napi::Value NativeImage::DrawText(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() < 4) {
        Napi::TypeError::New(env, "Missing parameters").ThrowAsJavaScriptException();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Invalid text").ThrowAsJavaScriptException();
    }
    const std::string text = info[0].As<Napi::String>().Utf8Value();

    if (text.length() == 0) {
        Napi::TypeError::New(env, "Invalid text").ThrowAsJavaScriptException();
    }

    if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Invalid topX").ThrowAsJavaScriptException();
    }
    const double topX = info[1].As<Napi::Number>().DoubleValue();

    if (!info[2].IsNumber()) {
        Napi::TypeError::New(env, "Invalid topY").ThrowAsJavaScriptException();
    }
    const double topY = info[2].As<Napi::Number>().DoubleValue();

    std::string color = "#000000";

    auto opts = VImage::option();

    if (info.Length() > 3) {
        if (!info[3].IsObject()) {
            Napi::TypeError::New(env, "Invalid options").ThrowAsJavaScriptException();
        }

        Napi::Object options = info[3].As<Napi::Object>();

        if (options.Has("font")) {
            if (!options.Get("font").IsString()) {
                Napi::TypeError::New(env, "Invalid font").ThrowAsJavaScriptException();
            }
//            std::cout << "font: " << options.Get("font").As<Napi::String>().Utf8Value() << std::endl;
            opts->set("font", options.Get("font").As<Napi::String>().Utf8Value().c_str());
        }

        if (options.Has("fontFile")) {
            if (!options.Get("fontFile").IsString()) {
                Napi::TypeError::New(env, "Invalid fontFile").ThrowAsJavaScriptException();
            }
//            std::cout << "fontFile: " << options.Get("fontFile").As<Napi::String>().Utf8Value() << std::endl;
//            opts->set("fontfile", options.Get("fontFile").As<Napi::String>().Utf8Value().c_str());
        }

        if (options.Has("color")) {
            if (!options.Get("color").IsString()) {
                Napi::TypeError::New(env, "Invalid color").ThrowAsJavaScriptException();
            }
            color = options.Get("color").As<Napi::String>().Utf8Value();

            if (color.at(0) != '#') {
                Napi::TypeError::New(env, "Invalid color").ThrowAsJavaScriptException();
            }           
        }
    }

    auto textColor = hexadecimalColorToARGB(color);

    // Create a new text image

    // this renders the text to a one-band image ... set width to the pixels across
    // of the area we want to render to to have it break lines for you
    VImage textImage = VImage::text(text.c_str(), opts);
    std::cout << "textImage width: " << textImage.width() << " height: " << textImage.height() << std::endl;
    
    // make a constant image the size of $text, but with every pixel red ... tag it
    // as srgb
    const std::vector<double> bgText = {double(textColor[1]), double(textColor[2]), double(textColor[3])};
    VImage textImageBg = textImage.new_from_image(bgText).copy(VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));

    // use the text mask as the alpha for the constant red image
    VImage overlay = textImageBg.bandjoin(textImage);

    // composite the text on the image
    this->image_ = this->image_.composite(overlay, VIPS_BLEND_MODE_OVER, VImage::option()->set("x", topX)->set("y", topY));

    return Napi::Number::New(env, 0);
}

/**
 * Save the image to a file
 */
Napi::Value NativeImage::Save(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::HandleScope scope(env);

    if (info.Length() == 0) {
        Napi::TypeError::New(env, "Missing file path").ThrowAsJavaScriptException();
    }

    if (!info[0].IsString()) {
        Napi::TypeError::New(env, "Invalid file path").ThrowAsJavaScriptException();
    }

    std::string path = info[0].As<Napi::String>().Utf8Value();
    this->image_.write_to_file(path.c_str());

    return Napi::Number::New(env, 0);
}

VImage NativeImage::createRGBImage(const CreationOptions& options) {
    std::vector<u_char> bgColor = hexadecimalColorToARGB(options.bgColor);
    std::vector<double> channels = {(double)bgColor[1], (double)bgColor[2], (double)bgColor[3]};

    // full image.
    VImage emptyImage = VImage::black(options.width,options.height, VImage::option()->set("bands", 3)) + channels;
    VImage formatted = emptyImage.cast(VipsBandFormat::VIPS_FORMAT_UCHAR).copy(VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB));

    return formatted;
}

VImage NativeImage::coloredTextImage(const std::string &text, const ColoredTextOptions& options) {
    auto genOpts = VImage::option();
    if (options.font.size() > 0) {
//        std::cout << "font " << options.font << std::endl;
        genOpts->set("font", options.font.c_str());
    }

    if (options.fontFile.size() > 0) {
        genOpts->set("fontfile", options.fontFile.c_str());
    }

    VImage textAlpha = VImage::text(text.c_str(), genOpts);

    // Do subtle adjustment to the image for alignment
    if (options.paddingBottom > 0 || options.paddingTop > 0) {
        int newHeight = textAlpha.height() + options.paddingTop + options.paddingBottom;
        int newWidth = textAlpha.width();

        // use default VIPS_EXTEND_BLACK option
        textAlpha = textAlpha.embed(0, options.paddingTop, newWidth, newHeight);
    }

//    std::cout << "render " << text << " xoffset " << textAlpha.xoffset() << " yoffset " << textAlpha.yoffset() << " width " << textAlpha.width() << " height " << textAlpha.height() << std::endl;
    if (options.width > 0 || options.height > 0) {
        int outWidth = options.width > 0 ? options.width : textAlpha.width();
        int outHeight = options.height > 0 ? options.height : textAlpha.height();

        if (outWidth < textAlpha.width() ) {
            printf("width value [%d] is smaller than the size of text [%d] and reset to text width\n", outWidth, textAlpha.width());
            outWidth = textAlpha.width();
        }

        if (outHeight < textAlpha.height() ) {
            printf("height value [%d] is smaller than the size of text [%d] and reset to text height\n", outHeight, textAlpha.height());
            outHeight = textAlpha.height();
        }

        textAlpha = textAlpha.gravity(options.textAlignment, outWidth, outHeight);

    }

    // make a constant image the size of $text, but with every pixel red ... tag it
    // as srgb
    const std::vector<double> textColor = {(double)options.textColor[0], (double)options.textColor[1], (double)options.textColor[2]};
    VImage coloredImage = textAlpha.new_from_image(textColor).copy(VImage::option()->set("interpretation", VIPS_INTERPRETATION_sRGB)).bandjoin(textAlpha);
//    std::cout << "return " << text << " xoffset " << coloredImage.xoffset() << " yoffset " << coloredImage.yoffset() << std::endl;
    return coloredImage;
}

CreationOptions NativeImage::parseCreationOptions(const Napi::Object& options) {
    CreationOptions opts;

    Napi::Value width = options.Get("width");
    if (width.IsNumber()) {
        opts.width = width.As<Napi::Number>().Int32Value();
    } else {
        Napi::TypeError::New(options.Env(), "The value of the width should be a number").ThrowAsJavaScriptException();
    }

    Napi::Value height = options.Get("height");
    if (height.IsNumber()) {
        opts.height = height.As<Napi::Number>().Int32Value();
    } else {
        Napi::TypeError::New(options.Env(), "The value of the height should be a number").ThrowAsJavaScriptException();
    }

    Napi::Value bgColor = options.Get("bgColor");
    if (bgColor.IsString()) {
        opts.bgColor = bgColor.As<Napi::String>().Utf8Value();
        if (opts.bgColor.at(0) != '#') {
            Napi::TypeError::New(options.Env(), "bgColor should be a valid hexadecimal string").ThrowAsJavaScriptException();
        }
    } else {
        Napi::TypeError::New(options.Env(), "bgColor should be a string").ThrowAsJavaScriptException();
    }

    return opts;
}

/**
 * Parse position options
 */
Position2D NativeImage::parsePosition2D(const Napi::Object& options) {
    Position2D position;

    // attribute "x" - required
    if (options.Has("x")) {
        if (options.Get("x").IsNumber()) {
            position.x = options.Get("x").As<Napi::Number>().Int32Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute x must be a number").ThrowAsJavaScriptException();
        }
    } else {
        Napi::TypeError::New(options.Env(), "Missing x attribute").ThrowAsJavaScriptException();
    }

    // attribute "y" - required
    if (options.Has("y")) {
        if (options.Get("y").IsNumber()) {
            position.y = options.Get("y").As<Napi::Number>().Int32Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute y must be a number").ThrowAsJavaScriptException();
        }
    } else {
        Napi::TypeError::New(options.Env(), "Missing y attribute").ThrowAsJavaScriptException();
    }

    // attribute "width" - optional
    if (options.Has("width")) {
        if (options.Get("width").IsNumber()) {
            position.width = options.Get("width").As<Napi::Number>().Int32Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute width must be a number").ThrowAsJavaScriptException();
        }
    }

    // attribute "height" - optional
    if (options.Has("height")) {
        if (options.Get("height").IsNumber()) {
            position.height = options.Get("height").As<Napi::Number>().Int32Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute height must be a number").ThrowAsJavaScriptException();
        }
    }

    return position;
}

/**
 * Parse countdown component options
 */
CountdownComponent NativeImage::parseCountdownComponent(const Napi::Object& options, bool ignoreText) {
    CountdownComponent component;

    // attribute "text" - required / optional
    if (options.Has("text")) {
        if (options.Get("text").IsString()) {
            component.text = options.Get("text").As<Napi::String>().Utf8Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute text must be a string.").ThrowAsJavaScriptException();
        }
    } else {
        if (!ignoreText) {
            Napi::TypeError::New(options.Env(), "Missing text attribute").ThrowAsJavaScriptException();
        }

    }

    // attribute "paddingTop" - optional
    if (options.Has("paddingTop")) {
        if (options.Get("paddingTop").IsNumber()) {
            component.paddingTop = options.Get("paddingTop").As<Napi::Number>().Int32Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute paddingTop must be a number").ThrowAsJavaScriptException();
        }
    }

    // attribute "paddingBottom" - optional
    if (options.Has("paddingBottom")) {
        if (options.Get("paddingBottom").IsNumber()) {
            component.paddingBottom = options.Get("paddingBottom").As<Napi::Number>().Int32Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute paddingBottom must be a number").ThrowAsJavaScriptException();
        }
    }

    // attribute "position" - required
    if (options.Has("position")) {
        if (options.Get("position").IsObject()) {
            Napi::Object positionObj = options.Get("position").As<Napi::Object>();
            component.position = parsePosition2D(positionObj);
        } else {
            Napi::TypeError::New(options.Env(), "Attribute position must be an object").ThrowAsJavaScriptException();
        }
    } else {
        Napi::TypeError::New(options.Env(), "Missing position attribute").ThrowAsJavaScriptException();
    }

    // attribute "color" - optional
    if (options.Has("color")) {
        if (options.Get("color").IsString()) {
            component.color = options.Get("color").As<Napi::String>().Utf8Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute color must be a string").ThrowAsJavaScriptException();
        }
    }

    // attribute "textAlignment" - optional
    if (options.Has("textAlignment")) {
        if (options.Get("textAlignment").IsString()) {
            component.textAlignment = jsvips::to_compass_direction(options.Get("textAlignment").As<Napi::String>().Utf8Value(), VipsCompassDirection::VIPS_COMPASS_DIRECTION_CENTRE);
        } else {
            Napi::TypeError::New(options.Env(), "Attribute textAlignment must be a string").ThrowAsJavaScriptException();
        }
    }

    // attribute "font" - optional
    if (options.Has("font")) {
        if (options.Get("font").IsString()) {
            component.font = options.Get("font").As<Napi::String>().Utf8Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute font must be a string").ThrowAsJavaScriptException();
        }
    }

    // attribute "fontFile" - optional
    if (options.Has("fontFile")) {
        if (options.Get("fontFile").IsString()) {
            component.fontFile = options.Get("fontFile").As<Napi::String>().Utf8Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute fontFile must be a string").ThrowAsJavaScriptException();
        }
    }

    return component;
}

CountdownComponentPosition NativeImage::parseCountdownComponentPosition(const Napi::Object& options) {
    CountdownComponentPosition cp;

    if (options.Has("position")) {
        if (options.Get("position").IsObject()) {
            Napi::Object positionObj = options.Get("position").As<Napi::Object>();
            cp.position = parsePosition2D(positionObj);
        } else {
            Napi::TypeError::New(options.Env(), "Attribute position must be an object").ThrowAsJavaScriptException();
        }
    } else {
        Napi::TypeError::New(options.Env(), "Missing position attribute").ThrowAsJavaScriptException();
    }

    return cp;
}

CountdownComponentStyle NativeImage::parseCountdownComponentStyle(const Napi::Object& options) {
    CountdownComponentStyle cs;

    if (options.Has("color")) {
        if (options.Get("color").IsString()) {
            cs.color = options.Get("color").As<Napi::String>().Utf8Value();
            if (cs.color.at(0) != '#') {
                Napi::TypeError::New(options.Env(), "Attribute color should be a valid hexadecimal string").ThrowAsJavaScriptException();
            }
        } else {
            Napi::TypeError::New(options.Env(), "Attribute color must be a string").ThrowAsJavaScriptException();
        }
    }

    // attribute "width" - optional
    if (options.Has("width")) {
        if (options.Get("width").IsNumber()) {
            cs.width = options.Get("width").As<Napi::Number>().Int32Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute width must be a number").ThrowAsJavaScriptException();
        }
    }

    // attribute "height" - optional
    if (options.Has("height")) {
        if (options.Get("height").IsNumber()) {
            cs.height = options.Get("height").As<Napi::Number>().Int32Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute height must be a number").ThrowAsJavaScriptException();
        }
    }

    if (options.Has("textAlignment")) {
        if (options.Get("textAlignment").IsString()) {
            cs.textAlignment = jsvips::to_compass_direction(options.Get("textAlignment").As<Napi::String>().Utf8Value(), VipsCompassDirection::VIPS_COMPASS_DIRECTION_CENTRE);
        } else {
            Napi::TypeError::New(options.Env(), "Attribute textAlignment must be a string").ThrowAsJavaScriptException();
        }
    }

    if (options.Has("font")) {
        if (options.Get("font").IsString()) {
            cs.font = options.Get("font").As<Napi::String>().Utf8Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute font must be a string").ThrowAsJavaScriptException();
        }
    }

    if (options.Has("fontFile")) {
        if (options.Get("fontFile").IsString()) {
            cs.fontFile = options.Get("fontFile").As<Napi::String>().Utf8Value();
        } else {
            Napi::TypeError::New(options.Env(), "Attribute fontFile must be a string").ThrowAsJavaScriptException();
        }
    }

    return cs;
}

/**
 * Parse countdown options
 */
CountdownOptions NativeImage::parseCountdownOptions(const Napi::Object& options) {
    CountdownOptions opts;

    CreationOptions creationOpts = parseCreationOptions(options);
    opts.width = creationOpts.width;
    opts.height = creationOpts.height;
    opts.bgColor = creationOpts.bgColor;

    // Attribute "labels" - required
    if (options.Has("labels")) {
        Napi::Value labels = options.Get("labels");
        if (labels.IsObject()) {
            Napi::Object labelsObj = labels.As<Napi::Object>();
            Napi::Array keys = labelsObj.GetPropertyNames();
            for (uint32_t i = 0; i < keys.Length(); i++) {
                std::string key = keys.Get(i).As<Napi::String>().Utf8Value();
                Napi::Value label = labelsObj.Get(key);
                if (label.IsObject()) {
                    Napi::Object labelObj = label.As<Napi::Object>();
                    CountdownComponent component = parseCountdownComponent(labelObj);
                    opts.labels[key] = component;
                } else {
                    Napi::TypeError::New(options.Env(), "Invalid format label object").ThrowAsJavaScriptException();
                }
            }
        } else {
            Napi::TypeError::New(options.Env(), "Parameter labels should be an object").ThrowAsJavaScriptException();
        }
    } else {
        Napi::TypeError::New(options.Env(), "Missing labels attribute").ThrowAsJavaScriptException();
    }

    // Attribute "digits" - required
    if (options.Has("digits")) {
        Napi::Value digits = options.Get("digits");
        if (digits.IsObject()) {
            Napi::Object digitsObj = digits.As<Napi::Object>();
            
            if (digitsObj.Has("positions")) {
                Napi::Value positions = digitsObj.Get("positions");
                if (positions.IsObject()) {
                    Napi::Object positionsObj = positions.As<Napi::Object>();
                    for (int i = 0; i < lengthOfCountdownMomentParts; i++) {
                        std::string k = countdownMomentPartNames[i];
                        if (positionsObj.Has(k)) {
                            if (positionsObj.Get(k).IsObject()) {
                                CountdownComponentPosition cp = parseCountdownComponentPosition(positionsObj.Get(k).As<Napi::Object>());
                                opts.digits.positions[i] = cp;
                            } else {
                                Napi::TypeError::New(options.Env(), "Invalid format position object").ThrowAsJavaScriptException();
                            }
                        } else {
                            Napi::TypeError::New(options.Env(), "Missing attribute " + k).ThrowAsJavaScriptException();
                        }
                    }
                } else {
                    Napi::TypeError::New(options.Env(), "Parameter positions should be an object").ThrowAsJavaScriptException();
                }
            } else {
                Napi::TypeError::New(options.Env(), "Missing positions attribute").ThrowAsJavaScriptException();
            }

            // Attribute "styles" - optional
            if (digitsObj.Has("style")) {
                Napi::Value styles = digitsObj.Get("style");
                if (styles.IsObject()) {
                    Napi::Object stylesObj = styles.As<Napi::Object>();
                    opts.digits.style = parseCountdownComponentStyle(stylesObj);
//                    std::cout << "style color: " << opts.digits.style.color << " width: " << opts.width << " height: " << opts.height << std::endl;
                } else {
                    Napi::TypeError::New(options.Env(), "Parameter styles should be an object").ThrowAsJavaScriptException();
                }
            }

            // Attribute "textTemplate" - optional
            if (digitsObj.Has("textTemplate")) {
                Napi::Value textTemplate = digitsObj.Get("textTemplate");
                if (textTemplate.IsString()) {
                    opts.digits.textTemplate = textTemplate.As<Napi::String>().Utf8Value();
                } else {
                    Napi::TypeError::New(options.Env(), "Parameter textTemplate should be a string").ThrowAsJavaScriptException();
                }
            }
        } else {
            Napi::TypeError::New(options.Env(), "Parameter digits should be an object").ThrowAsJavaScriptException();
        }
    } else {
        Napi::TypeError::New(options.Env(), "Missing digits attribute").ThrowAsJavaScriptException();
    }

    return opts;
}

std::vector<int> NativeImage::parseCountdownMomentWithNumber(const Napi::Object &options) {
    std::vector<int> moment;

    for (int i = 0; i < lengthOfCountdownMomentParts; i++) {
        std::string k = countdownMomentPartNames[i];
        if (options.Has(k)) {
            if (options.Get(k).IsNumber()) {
                moment.push_back(options.Get(k).As<Napi::Number>().Int32Value());
            } else {
                Napi::TypeError::New(options.Env(), "Attribute " + k + " must be a number").ThrowAsJavaScriptException();
            }
        } else {
            Napi::TypeError::New(options.Env(), "Missing attribute " + k).ThrowAsJavaScriptException();
        }
    }

    return moment;
}

/**
 * Convert hex color string to RGB
 * 
 * Allowed formats:
 * [#]RGB
 * [#]ARGB
 * [#]RRGGBB
 * [#]AARRGGBB
 *
 * @param hex color hex string, should be start with "#" and the rest length is 3, 4, 6 or 8 characters
 * @return array a decimal ARGB array, return black if the hex string is invalid
 * 
 */
std::vector<u_char> NativeImage::hexadecimalColorToARGB(const std::string& hex) {
    const std::vector<u_char> defaultColor = {0, 0, 0, 0};
    if (hex.length() == 0) {
        return defaultColor;
    }

    if (hex.length() != 4 && hex.length() != 5 && hex.length() != 7 && hex.length() != 9) {
        return defaultColor;
    }

    if (hex.at(0) != '#') {
        return defaultColor;
    }

    std::string a,r,g,b;
    int withAlpha = 0;

    if (hex.length() == 4 || hex.length() == 5) {
        if ( hex.length() == 5 ) {
            withAlpha = 1;
            a = hex.substr(1, 1);
        } else {
            a = "FF";
        }

        r = hex.substr(1 + withAlpha, 1) + hex.substr(1 + withAlpha, 1);
        g = hex.substr(2 + withAlpha, 1) + hex.substr(2 + withAlpha, 1);
        b = hex.substr(3 + withAlpha, 1) + hex.substr(3 + withAlpha, 1);
    } else if (hex.length() == 7 || hex.length() == 9) {
        if ( hex.length() == 9 ) {
            withAlpha = 1;
            a = hex.substr(1, 2);
        } else {
            a = "FF";
        }
        r = hex.substr(1 + 2*withAlpha, 2);
        g = hex.substr(3 + 2*withAlpha, 2);
        b = hex.substr(5 + 2*withAlpha, 2);
    }

    // convert hex to decimal
    u_char alpha = std::stoi(a, nullptr, 16);
    u_char red = std::stoi(r, nullptr, 16);
    u_char green = std::stoi(g, nullptr, 16);
    u_char blue = std::stoi(b, nullptr, 16);

    return {alpha, red, green, blue};
}

VImage NativeImage::renderCountdownAnimation(const std::vector<int> &duration, int frames) {
    if ( this->mode_ != ImageMode::COUNTDOWN ) {
        throw std::invalid_argument("The object is not initialized with countdown mode");
    }

    int numFrames = frames > 0 ? frames : 1;

    std::vector<VImage> pages;
    std::vector<int> newDuration = duration;

    // Build the position array
    std::vector<int> xLabel;
    std::vector<int> yLabel;
    for (auto cp : this->countdownOptions_.digits.positions) {
        xLabel.push_back(cp.position.x);
        yLabel.push_back(cp.position.y);
    }
    std::vector<int> modes = {VipsBlendMode::VIPS_BLEND_MODE_OVER};

    for (int i = 0; i < numFrames; i++) {

        std::vector<VImage> subImages;
        // Add background image
        subImages.push_back(this->image_);

        for (int j = 0; j < lengthOfCountdownMomentParts; j++) {
            int digitValue = newDuration.at(j);
            std::string k = countdownMomentPartNames[j];
            subImages.push_back(this->countdownDigits_.at(digitValue));
        }

        // Build a frame
        VImage page = VImage::composite(subImages, modes, VImage::option()->set("x", xLabel)->set("y", yLabel));
        pages.push_back(page);

        // Next frame
        newDuration = minusOneSecondToDuration(newDuration);
    }

    // Join a set of pages vertically to make a multipage image
    VImage animation = VImage::arrayjoin(pages, VImage::option()->set("across", 1));
    VImage gifData = animation.copy();
    gifData.set("page-height", this->image_.height());

    // frame delays are in milliseconds ... 300 is pretty slow!
    std::vector<int> delayArray(pages.size(), 1000);
    gifData.set("delay", delayArray);

    return gifData;
}

//
// Prepare resources to generate countdown animation
//
Napi::Value NativeImage::CreateCountdownAnimation(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);

    if (info.Length() == 0) {
        Napi::TypeError::New(env, "Missing CountdownOptions").ThrowAsJavaScriptException();
    }

    // Retrieve the instance data we stored during `Init()`. We only stored the
    // constructor there, so we retrieve it here to create a new instance of the
    // JS class the constructor represents.
    Napi::Value mode = Napi::Number::New(env, static_cast<int>(ImageMode::COUNTDOWN));
    Napi::FunctionReference* constructor = env.GetInstanceData<Napi::FunctionReference>();
    Napi::Object obj = constructor->New({info[0], mode});
    return scope.Escape(napi_value(obj)).ToObject();
}

/**
 *   renderCountdownAnimation(start: CountdownMoment<number>, frames: number, toFile?: string): Buffer | string;
 * @param info
 * @return
 */
Napi::Value NativeImage::RenderCountdownAnimation(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    Napi::EscapableHandleScope scope(env);

    if (info.Length() < 2) {
        Napi::TypeError::New(env, "At least 2 parameter are required!").ThrowAsJavaScriptException();
    }

    if (!info[0].IsObject()) {
        Napi::TypeError::New(env, "Invalid start time object").ThrowAsJavaScriptException();
    }
    Napi::Object startObj = info[0].As<Napi::Object>();
    std::vector<int> start = parseCountdownMomentWithNumber(startObj);

    if (!info[1].IsNumber()) {
        Napi::TypeError::New(env, "Invalid frames number").ThrowAsJavaScriptException();
    }
    int frames = info[1].As<Napi::Number>().Int32Value();

    std::string outputFilePath;
    if (info.Length() >= 3) {
        // Directly save to file
        if (!info[2].IsString()) {
            Napi::TypeError::New(env, "Invalid file path").ThrowAsJavaScriptException();
        }
        outputFilePath = info[2].As<Napi::String>().Utf8Value();
    }

    // Generate animation
    VImage gifImage = renderCountdownAnimation(start, frames);
    std::cout << "Render countdown animation with " << frames << " frames " << gifImage.height() << " height, " << gifImage.width()<< std::endl;

    // Return buffer or save to file
    if (outputFilePath.empty()) {
        // write to a formatted memory buffer
        size_t size;
        void *buf;
        gifImage.write_to_buffer(".gif", &buf, &size);
        return Napi::Buffer<char>::Copy(env, static_cast<char*>(buf), size);
    } else {
        gifImage.write_to_file(outputFilePath.c_str());
        return Napi::String::New(env, outputFilePath);
    }
}

std::vector<int> NativeImage::minusOneSecondToDuration(const std::vector<int>& duration) {
    int size = duration.size();
    if (size != lengthOfCountdownMomentParts) {
        throw std::invalid_argument("Invalid duration size");
    }

    std::vector<int> newDuration = duration;
    newDuration.at(static_cast<int>(CountdownMomentPart::SECONDS)) -= 1;

    if (newDuration.at(static_cast<int>(CountdownMomentPart::SECONDS)) < 0) {
        newDuration.at(static_cast<int>(CountdownMomentPart::SECONDS)) += 60;
        newDuration.at(static_cast<int>(CountdownMomentPart::MINUTES)) -= 1;

        if (newDuration.at(static_cast<int>(CountdownMomentPart::MINUTES)) < 0) {
            newDuration.at(static_cast<int>(CountdownMomentPart::MINUTES)) += 60;
            newDuration.at(static_cast<int>(CountdownMomentPart::HOURS)) -= 1;

            if (newDuration.at(static_cast<int>(CountdownMomentPart::HOURS)) < 0) {
                newDuration.at(static_cast<int>(CountdownMomentPart::HOURS)) += 24;
                newDuration.at(static_cast<int>(CountdownMomentPart::DAYS)) -= 1;

                if (newDuration.at(static_cast<int>(CountdownMomentPart::DAYS)) < 0) {
                    // Reset all to 0
                    newDuration.at(static_cast<int>(CountdownMomentPart::DAYS)) = 0;
                    newDuration.at(static_cast<int>(CountdownMomentPart::HOURS)) = 0;
                    newDuration.at(static_cast<int>(CountdownMomentPart::MINUTES)) = 0;
                    newDuration.at(static_cast<int>(CountdownMomentPart::SECONDS)) = 0;
                }
            }
        }
    }

    return newDuration;
}
