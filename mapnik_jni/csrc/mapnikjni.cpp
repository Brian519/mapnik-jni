#include <jni.h>
#include <mapnik/map.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/attribute_collector.hpp>

#include "mapnikjni.h"

//// -- Globals
struct classinfo_t {
	jclass java_class;
	jfieldID ptr_field;
	jfieldID parentref_field;
};

static bool initialized=false;
static classinfo_t
	CLASS_MAP,
	CLASS_LAYER,
	CLASS_DATASOURCE,
	CLASS_DATASOURCE_CACHE,
	CLASS_FEATURE_TYPE_STYLE;
static jclass
	CLASS_STRING,
	CLASS_HASHSET,
	CLASS_PARAMETERS,
	CLASS_BOX2D,
	CLASS_COLOR;
static jmethodID
	CTOR_PARAMETERS,
	METHOD_PARAMETERS_SET_STRING,
	METHOD_PARAMETERS_SET_INT,
	METHOD_PARAMETERS_SET_DOUBLE,
	METHOD_PARAMETERS_COPY_TO_NATIVE,
	CTOR_HASHSET,
	METHOD_HASHSET_ADD;
static jfieldID
	FIELD_BOX2D_MINX,
	FIELD_BOX2D_MINY,
	FIELD_BOX2D_MAXX,
	FIELD_BOX2D_MAXY,

	FIELD_COLOR_RED,
	FIELD_COLOR_GREEN,
	FIELD_COLOR_BLUE,
	FIELD_COLOR_ALPHA;

void throw_error(JNIEnv* env, const char* msg) {
	jclass clazz=env->FindClass("java/lang/Error");
	env->ThrowNew(clazz, msg);
}

void throw_java_exception(JNIEnv* env, std::exception& e) {
	jclass clazz=env->FindClass("java/lang/RuntimeException");
	env->ThrowNew(clazz, e.what());
}

#define ASSERT_INITIALIZED if (!initialized) throw_error(env, "Library not initialized")
#define TO_POINTER(ptr) ((void*)ptr)
#define FROM_POINTER(ptr) ((jlong)ptr)

// Pointer access macros
#define LOAD_MAP_POINTER(mapobj) (static_cast<mapnik::Map*>((void*)(env->GetLongField(mapobj, CLASS_MAP.ptr_field))))
#define LOAD_LAYER_POINTER(layerobj) (static_cast<mapnik::layer*>((void*)(env->GetLongField(layerobj, CLASS_LAYER.ptr_field))))
#define LOAD_DATASOURCE_POINTER(dsobj) (static_cast<mapnik::datasource_ptr*>((void*)(env->GetLongField(dsobj, CLASS_DATASOURCE.ptr_field))))
#define LOAD_FEATURE_TYPE_STYLE_POINTER(styleobj) (static_cast<mapnik::feature_type_style*>((void*)(env->GetLongField(styleobj, CLASS_FEATURE_TYPE_STYLE.ptr_field))))

static bool init_class(JNIEnv* env, const char* name, classinfo_t& ci, bool has_parentref) {
	//printf("Initing %s\n", name);

	ci.ptr_field=0;
	ci.parentref_field=0;
	ci.java_class=(jclass)env->NewGlobalRef(env->FindClass(name));
	if (!ci.java_class) return false;

	ci.ptr_field=env->GetFieldID(ci.java_class, "ptr", "J");
	if (!ci.ptr_field) return false;

	if (has_parentref) {
		ci.parentref_field=env->GetFieldID(ci.java_class, "parentref", "Ljava/lang/Object;");
		if (!ci.parentref_field) {
			return false;
		}
	}

	//printf("Inited %s\n", name);
	return true;
}

static bool init_ids(JNIEnv* env) {
	// Load classes
	if (!(
		init_class(env, "mapnik/Map", CLASS_MAP, false) &&
		init_class(env, "mapnik/Datasource", CLASS_DATASOURCE, false) &&
		init_class(env, "mapnik/DatasourceCache", CLASS_DATASOURCE_CACHE, false) &&
		init_class(env, "mapnik/Layer", CLASS_LAYER, false) &&
		init_class(env, "mapnik/FeatureTypeStyle", CLASS_FEATURE_TYPE_STYLE, false)
		)) {
		throw_error(env, "Error initializing native references");
		return false;
	}

	// String
	CLASS_STRING=(jclass)env->NewGlobalRef(env->FindClass("java/lang/String"));

	// Parameters
	CLASS_PARAMETERS=(jclass)env->NewGlobalRef(env->FindClass("mapnik/Parameters"));
	CTOR_PARAMETERS=env->GetMethodID(CLASS_PARAMETERS, "<init>", "()V");
	METHOD_PARAMETERS_SET_STRING=env->GetMethodID(CLASS_PARAMETERS, "setString", "(Ljava/lang/String;Ljava/lang/String;)V");
	METHOD_PARAMETERS_SET_INT=env->GetMethodID(CLASS_PARAMETERS, "setInt", "(Ljava/lang/String;I)V");
	METHOD_PARAMETERS_SET_DOUBLE=env->GetMethodID(CLASS_PARAMETERS, "setDouble", "(Ljava/lang/String;D)V");
	METHOD_PARAMETERS_COPY_TO_NATIVE=env->GetMethodID(CLASS_PARAMETERS, "copyToNative", "(J)V");

	// HashSet
	CLASS_HASHSET=(jclass)env->NewGlobalRef(env->FindClass("java/util/HashSet"));
	CTOR_HASHSET=env->GetMethodID(CLASS_HASHSET, "<init>", "()V");
	METHOD_HASHSET_ADD=env->GetMethodID(CLASS_HASHSET, "add", "(Ljava/lang/Object;)Z");

	// Box2d
	CLASS_BOX2D=(jclass)env->NewGlobalRef(env->FindClass("mapnik/Box2d"));
	FIELD_BOX2D_MINX=env->GetFieldID(CLASS_BOX2D, "minx", "D");
	FIELD_BOX2D_MINY=env->GetFieldID(CLASS_BOX2D, "miny", "D");
	FIELD_BOX2D_MAXX=env->GetFieldID(CLASS_BOX2D, "maxx", "D");
	FIELD_BOX2D_MAXY=env->GetFieldID(CLASS_BOX2D, "maxy", "D");

	// Color
	CLASS_COLOR=(jclass)env->NewGlobalRef(env->FindClass("mapnik/Color"));
	FIELD_COLOR_RED=env->GetFieldID(CLASS_COLOR, "red", "I");
	FIELD_COLOR_GREEN=env->GetFieldID(CLASS_COLOR, "green", "I");
	FIELD_COLOR_BLUE=env->GetFieldID(CLASS_COLOR, "blue", "I");
	FIELD_COLOR_ALPHA=env->GetFieldID(CLASS_COLOR, "alpha", "I");

	return true;
}

class refjavastring {
	JNIEnv* env;
	jstring js;
	jboolean isCopy;
public:
	const char* stringz;
	bool isnull;

	refjavastring(JNIEnv* env, jstring js) {
		this->env=env;
		this->js=js;
		if (js) {
			stringz=env->GetStringUTFChars(js, &isCopy);
			isnull=false;
		} else {
			stringz="";
			isnull=true;
			isCopy=0;
		}
	}
	~refjavastring() {
		if (isCopy) {
			env->ReleaseStringUTFChars(js, stringz);
		}
	}
};

static jobject box2dFromNative(JNIEnv *env, mapnik::box2d<double> const& box) {
	jobject ret=env->AllocObject(CLASS_BOX2D);
	env->SetDoubleField(ret, FIELD_BOX2D_MINX, box.minx());
	env->SetDoubleField(ret, FIELD_BOX2D_MINY, box.miny());
	env->SetDoubleField(ret, FIELD_BOX2D_MAXX, box.maxx());
	env->SetDoubleField(ret, FIELD_BOX2D_MAXY, box.maxy());
	return ret;
}

static mapnik::box2d<double> box2dToNative(JNIEnv *env, jobject box) {
	mapnik::box2d<double> ret(
			env->GetDoubleField(box, FIELD_BOX2D_MINX),
			env->GetDoubleField(box, FIELD_BOX2D_MINY),
			env->GetDoubleField(box, FIELD_BOX2D_MAXX),
			env->GetDoubleField(box, FIELD_BOX2D_MAXY)
			);
	return ret;
}

static mapnik::color colorToNative(JNIEnv *env, jobject c) {
	return mapnik::color(
			env->GetIntField(c, FIELD_COLOR_RED),
			env->GetIntField(c, FIELD_COLOR_GREEN),
			env->GetIntField(c, FIELD_COLOR_BLUE),
			env->GetIntField(c, FIELD_COLOR_ALPHA)
			);
}

static jobject colorFromNative(JNIEnv *env, mapnik::color const& c) {
	jobject ret=env->AllocObject(CLASS_COLOR);
	env->SetIntField(ret, FIELD_COLOR_RED, c.red());
	env->SetIntField(ret, FIELD_COLOR_GREEN, c.green());
	env->SetIntField(ret, FIELD_COLOR_BLUE, c.blue());
	env->SetIntField(ret, FIELD_COLOR_ALPHA, c.alpha());
}

//// --- Parameters
static void translate_to_mapnik_parameters(JNIEnv *env, jobject javaparams, mapnik::parameters& mapnikparams)
{
	if (!javaparams) return;
	env->CallVoidMethod(javaparams, METHOD_PARAMETERS_COPY_TO_NATIVE, (jlong)(&mapnikparams));
}

class translate_parameter_visitor: public boost::static_visitor<>
{
	JNIEnv *env;
	jobject paramobject;
	jstring key;
public:
	translate_parameter_visitor(JNIEnv* aenv, jobject aparamobject, jstring akey): env(aenv), paramobject(aparamobject), key(akey) {
	}

	void operator()(const int& value) const {
		env->CallVoidMethod(paramobject, METHOD_PARAMETERS_SET_INT, key, (jint)value);
	}

	void operator()(const std::string& value) const {
		env->CallVoidMethod(paramobject, METHOD_PARAMETERS_SET_STRING,
				key,
				env->NewStringUTF(value.c_str()));
	}

	void operator()(const double& value) const {
		env->CallVoidMethod(paramobject, METHOD_PARAMETERS_SET_DOUBLE, key, (jdouble)value);
	}
};

/*
 * Class:     mapnik_Parameters
 * Method:    setNativeInt
 * Signature: (JLjava/lang/String;I)V
 */
JNIEXPORT void JNICALL Java_mapnik_Parameters_setNativeInt
  (JNIEnv *env, jclass c, jlong ptr, jstring namej, jint value)
{
	refjavastring name(env, namej);
	mapnik::parameters *params=(mapnik::parameters*)(ptr);
	(*params)[name.stringz]=(int)value;
}

/*
 * Class:     mapnik_Parameters
 * Method:    setNativeString
 * Signature: (JLjava/lang/String;Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Parameters_setNativeString
  (JNIEnv *env, jclass c, jlong ptr, jstring namej, jstring valuej)
{
	refjavastring name(env, namej);
	refjavastring value(env, valuej);
	mapnik::parameters *params=(mapnik::parameters*)(ptr);
	(*params)[name.stringz]=value.stringz;
}


/*
 * Class:     mapnik_Parameters
 * Method:    setNativeDouble
 * Signature: (JLjava/lang/String;D)V
 */
JNIEXPORT void JNICALL Java_mapnik_Parameters_setNativeDouble
  (JNIEnv *env, jclass c, jlong ptr, jstring namej, jdouble value)
{
	refjavastring name(env, namej);
	mapnik::parameters *params=(mapnik::parameters*)(ptr);
	(*params)[name.stringz]=(double)value;
}


//// --- Map class members
/*
 * Class:     mapnik_Map
 * Method:    alloc
 * Signature: (IILjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_mapnik_Map_alloc__IILjava_lang_String_2
  (JNIEnv *env, jclass c, jint width, jint height, jstring srsj)
{
	ASSERT_INITIALIZED;
	refjavastring srs(env, srsj);

	return FROM_POINTER(new mapnik::Map(width, height, srs.stringz));
}

/*
 * Class:     mapnik_Map
 * Method:    alloc
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_mapnik_Map_alloc__
  (JNIEnv *env, jclass c)
{
	ASSERT_INITIALIZED;
	return FROM_POINTER(new mapnik::Map());
}

/*
 * Class:     mapnik_Map
 * Method:    dealloc
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_dealloc
  (JNIEnv *env, jclass c, jlong ptr)
{
	if (ptr) {
		delete static_cast<mapnik::Map*>(TO_POINTER(ptr));
	}
}

/*
 * Class:     mapnik_Map
 * Method:    loadMap
 * Signature: (Ljava/lang/String;Z)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_loadMap
  (JNIEnv *env, jobject mapobject, jstring filenamej, jboolean strict)
{
	try {
		mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
		refjavastring filename(env, filenamej);
		mapnik::load_map(*map, filename.stringz, (bool)strict);
	} catch (std::exception& e) {
		throw_java_exception(env, e);
		return;
	}
}

/*
 * Class:     mapnik_Map
 * Method:    loadMapString
 * Signature: (Ljava/lang/String;ZLjava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_loadMapString
  (JNIEnv *env, jobject mapobject, jstring strj, jboolean strict, jstring basepathj)
{
	try {
		mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
		refjavastring str(env, strj);
		refjavastring basepath(env, basepathj);
		mapnik::load_map_string(*map, str.stringz, (bool)strict, basepath.stringz);
	} catch (std::exception& e) {
		throw_java_exception(env, e);
		return;
	}
}

/*
 * Class:     mapnik_Map
 * Method:    getLayerCount
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_mapnik_Map_getLayerCount
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	return (jint)map->layer_count();
}

/*
 * Class:     mapnik_Map
 * Method:    getLayer
 * Signature: (I)Lmapnik/Layer;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Map_getLayer
  (JNIEnv *env, jobject mapobject, jint index)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	std::vector<mapnik::layer> &layers(map->layers());
	if (index<0 || index>layers.size()) {
		return 0;
	}

	mapnik::layer* layer_copy=new mapnik::layer(layers[index]);
	jobject layer=env->AllocObject(CLASS_LAYER.java_class);
	env->SetLongField(layer, CLASS_LAYER.ptr_field, FROM_POINTER(layer_copy));
	return layer;
}

/*
 * Class:     mapnik_Map
 * Method:    setLayer
 * Signature: (ILmapnik/Layer;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_setLayer
  (JNIEnv *env, jobject mapobject, jint index, jobject layerobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	std::vector<mapnik::layer> &layers(map->layers());
	if (index<0 || index>layers.size()) {
		return;
	}

	if (!layerobject) return;
	mapnik::layer* layer=
			static_cast<mapnik::layer*>(TO_POINTER(env->GetLongField(layerobject, CLASS_LAYER.ptr_field)));
	layers[index]=*layer;
}


/*
 * Class:     mapnik_Map
 * Method:    removeLayer
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_removeLayer
  (JNIEnv *env, jobject mapobject, jint index)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->removeLayer(index);
}

/*
 * Class:     mapnik_Map
 * Method:    addLayer
 * Signature: (Lmapnik/Layer;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_addLayer
  (JNIEnv *env, jobject mapobject, jobject layerobject)
{
	if (!layerobject) return;

	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	mapnik::layer* layer=
			static_cast<mapnik::layer*>(TO_POINTER(env->GetLongField(layerobject, CLASS_LAYER.ptr_field)));

	map->addLayer(*layer);
}

/*
 * Class:     mapnik_Map
 * Method:    removeAllLayers
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_removeAllLayers
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	std::vector<mapnik::layer> &layers(map->layers());
	layers.clear();
}

/*
 * Class:     mapnik_Map
 * Method:    getWidth
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_mapnik_Map_getWidth
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	return (jint)map->width();
}

/*
 * Class:     mapnik_Map
 * Method:    getHeight
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_mapnik_Map_getHeight
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	return (jint)map->height();
}

/*
 * Class:     mapnik_Map
 * Method:    setWidth
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_setWidth
  (JNIEnv *env, jobject mapobject, jint width)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->set_width(width);
}

/*
 * Class:     mapnik_Map
 * Method:    setHeight
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_setHeight
  (JNIEnv *env, jobject mapobject, jint height)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->set_height(height);
}

/*
 * Class:     mapnik_Map
 * Method:    resize
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_resize
  (JNIEnv *env, jobject mapobject, jint width, jint height)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->resize(width, height);
}

/*
 * Class:     mapnik_Map
 * Method:    getSrs
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_mapnik_Map_getSrs
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	return env->NewStringUTF(map->srs().c_str());
}

/*
 * Class:     mapnik_Map
 * Method:    setSrs
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_mapnik_Map_setSrs
  (JNIEnv *env, jobject mapobject, jstring srsj)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	refjavastring srs(env, srsj);

	map->set_srs(srs.stringz);
}

/*
 * Class:     mapnik_Map
 * Method:    setBufferSize
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_setBufferSize
  (JNIEnv *env, jobject mapobject, jint s)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->set_buffer_size(s);
}

/*
 * Class:     mapnik_Map
 * Method:    getBufferSize
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_mapnik_Map_getBufferSize
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	return (jint)map->buffer_size();
}

/*
 * Class:     mapnik_Map
 * Method:    getBasePath
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_mapnik_Map_getBasePath
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	return env->NewStringUTF(map->base_path().c_str());
}

/*
 * Class:     mapnik_Map
 * Method:    setBasePath
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_setBasePath
  (JNIEnv *env, jobject mapobject, jstring basepathj)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	refjavastring basepath(env, basepathj);
	map->set_base_path(basepath.stringz);
}

/*
 * Class:     mapnik_Map
 * Method:    getStyleNames
 * Signature: ()Ljava/util/Collection;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Map_getStyleNames
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	jobject ret=env->NewObject(CLASS_HASHSET, CTOR_HASHSET);
	for (mapnik::Map::style_iterator iter=map->begin_styles(); iter!=map->end_styles(); iter++) {
		jstring name=env->NewStringUTF(iter->first.c_str());
		env->CallBooleanMethod(ret, METHOD_HASHSET_ADD, name);
	}

	return ret;
}

/*
 * Class:     mapnik_Map
 * Method:    getStyle
 * Signature: (Ljava/lang/String;)Lmapnik/FeatureTypeStyle;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Map_getStyle
  (JNIEnv *env, jobject mapobject, jstring namej)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	refjavastring name(env, namej);
	std::string namestring(name.stringz);

	boost::optional<mapnik::feature_type_style const&> style=map->find_style(namestring);
	if (!style) return 0;

	mapnik::feature_type_style* stylepinned=new mapnik::feature_type_style(style.get());

	jobject ret=env->AllocObject(CLASS_FEATURE_TYPE_STYLE.java_class);
	env->SetLongField(ret, CLASS_FEATURE_TYPE_STYLE.ptr_field, FROM_POINTER(stylepinned));
	return ret;
}

/*
 * Class:     mapnik_Map
 * Method:    addStyle
 * Signature: (Ljava/lang/String;Lmapnik/FeatureTypeStyle;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_addStyle
  (JNIEnv *env, jobject mapobject, jstring namej, jobject styleobject)
{
	if (!styleobject) return;

	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	refjavastring name(env, namej);
	mapnik::feature_type_style* style=static_cast<mapnik::feature_type_style*>(
			TO_POINTER(env->GetLongField(styleobject, CLASS_FEATURE_TYPE_STYLE.ptr_field))
			);
	map->insert_style(name.stringz, *style);
}

/*
 * Class:     mapnik_Map
 * Method:    removeStyle
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_removeStyle
  (JNIEnv *env, jobject mapobject, jstring namej)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	refjavastring name(env, namej);
	map->remove_style(name.stringz);
}

/*
 * Class:     mapnik_Map
 * Method:    getMaximumExtent
 * Signature: ()Lmapnik/Box2d;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Map_getMaximumExtent
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	boost::optional<mapnik::box2d<double> > extent=map->maximum_extent();
	if (!extent) return 0;

	return box2dFromNative(env, extent.get());
}

/*
 * Class:     mapnik_Map
 * Method:    setMaximumExtent
 * Signature: (Lmapnik/Box2d;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_setMaximumExtent
  (JNIEnv *env, jobject mapobject, jobject extentj)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	if (!extentj) return;

	map->set_maximum_extent(
			box2dToNative(env, extentj));
}

/*
 * Class:     mapnik_Map
 * Method:    getCurrentExtent
 * Signature: ()Lmapnik/Box2d;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Map_getCurrentExtent
	(JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	mapnik::box2d<double> extent=map->get_current_extent();
	return box2dFromNative(env, extent);
}

/*
 * Class:     mapnik_Map
 * Method:    getBufferedExtent
 * Signature: ()Lmapnik/Box2d;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Map_getBufferedExtent
	(JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	mapnik::box2d<double> extent=map->get_buffered_extent();
	return box2dFromNative(env, extent);
}

/*
 * Class:     mapnik_Map
 * Method:    zoom
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_zoom
  (JNIEnv *env, jobject mapobject, jdouble z)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->zoom(z);
}

/*
 * Class:     mapnik_Map
 * Method:    zoomToBox
 * Signature: (Lmapnik/Box2d;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_zoomToBox
  (JNIEnv *env, jobject mapobject, jobject box)
{
	if (!box) return;

	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->zoom_to_box(box2dToNative(env, box));
}

/*
 * Class:     mapnik_Map
 * Method:    zoomAll
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_zoomAll
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->zoom_all();
}

/*
 * Class:     mapnik_Map
 * Method:    pan
 * Signature: (II)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_pan
  (JNIEnv *env, jobject mapobject, jint x, jint y)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->pan(x, y);
}

/*
 * Class:     mapnik_Map
 * Method:    panAndZoom
 * Signature: (IID)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_panAndZoom
  (JNIEnv *env, jobject mapobject, jint x, jint y, jdouble z)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->pan_and_zoom(x, y, z);
}

/*
 * Class:     mapnik_Map
 * Method:    getScale
 * Signature: ()D
 */
JNIEXPORT jdouble JNICALL Java_mapnik_Map_getScale
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	return map->scale();
}

/*
 * Class:     mapnik_Map
 * Method:    getScaleDenominator
 * Signature: ()D
 */
JNIEXPORT jdouble JNICALL Java_mapnik_Map_getScaleDenominator
	(JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	return map->scale_denominator();
}

/*
 * Class:     mapnik_Map
 * Method:    getBackground
 * Signature: ()Lmapnik/Color;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Map_getBackground
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	boost::optional<mapnik::color> const& color(map->background());
	if (!color) return 0;

	return colorFromNative(env, color.get());
}

/*
 * Class:     mapnik_Map
 * Method:    setBackground
 * Signature: (Lmapnik/Color;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_setBackground
  (JNIEnv *env, jobject mapobject, jobject c)
{
	if (!c) return;
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	map->set_background(colorToNative(env, c));
}

/*
 * Class:     mapnik_Map
 * Method:    getBackgroundImage
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_mapnik_Map_getBackgroundImage
  (JNIEnv *env, jobject mapobject)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	boost::optional<std::string> const& image(map->background_image());
	if (!image) return 0;

	return env->NewStringUTF(image.get().c_str());
}

/*
 * Class:     mapnik_Map
 * Method:    setBackgroundImage
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Map_setBackgroundImage
  (JNIEnv *env, jobject mapobject, jstring filenamej)
{
	mapnik::Map* map=LOAD_MAP_POINTER(mapobject);
	if (!filenamej) return;
	refjavastring filename(env, filenamej);
	map->set_background_image(filename.stringz);
}


//// -- Layer class
/*
 * Class:     mapnik_Layer
 * Method:    alloc
 * Signature: (Ljava/lang/String;Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_mapnik_Layer_alloc
  (JNIEnv *env, jclass c, jstring namej, jstring srsj)
{
	ASSERT_INITIALIZED;

	refjavastring name(env, namej);
	refjavastring srs(env, srsj);

	mapnik::layer* layer=new mapnik::layer(name.stringz, srs.stringz);
	return FROM_POINTER(layer);
}

/*
 * Class:     mapnik_Layer
 * Method:    dealloc
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_dealloc
  (JNIEnv *env, jclass c, jlong ptr)
{
	if (ptr) {
		delete static_cast<mapnik::layer*>(TO_POINTER(ptr));
	}
}

/*
 * Class:     mapnik_Layer
 * Method:    getName
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_mapnik_Layer_getName
  (JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return env->NewStringUTF(layer->name().c_str());
}

/*
 * Class:     mapnik_Layer
 * Method:    setName
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setName
  (JNIEnv *env, jobject layerobj, jstring sj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	refjavastring s(env, sj);
	layer->set_name(s.stringz);
}

/*
 * Class:     mapnik_Layer
 * Method:    getTitle
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_mapnik_Layer_getTitle
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return env->NewStringUTF(layer->title().c_str());
}

/*
 * Class:     mapnik_Layer
 * Method:    setTitle
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setTitle
(JNIEnv *env, jobject layerobj, jstring sj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	refjavastring s(env, sj);
	layer->set_title(s.stringz);
}

/*
 * Class:     mapnik_Layer
 * Method:    getAbstract
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_mapnik_Layer_getAbstract
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return env->NewStringUTF(layer->abstract().c_str());
}

/*
 * Class:     mapnik_Layer
 * Method:    setAbstract
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setAbstract
(JNIEnv *env, jobject layerobj, jstring sj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	refjavastring s(env, sj);
	layer->set_abstract(s.stringz);
}

/*
 * Class:     mapnik_Layer
 * Method:    getSrs
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_mapnik_Layer_getSrs
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return env->NewStringUTF(layer->srs().c_str());
}

/*
 * Class:     mapnik_Layer
 * Method:    setSrs
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setSrs
(JNIEnv *env, jobject layerobj, jstring sj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	refjavastring s(env, sj);
	layer->set_srs(s.stringz);
}

/*
 * Class:     mapnik_Layer
 * Method:    getStyles
 * Signature: ()[Ljava/lang/String;
 */
JNIEXPORT jobjectArray JNICALL Java_mapnik_Layer_getStyles
  (JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	std::vector<std::string> &styles(layer->styles());

	jobjectArray ary=env->NewObjectArray(
			styles.size(),
			CLASS_STRING,
			(jobject)0);

	for (unsigned i=0; i<styles.size(); i++) {
		env->SetObjectArrayElement(ary, i,
				env->NewStringUTF(styles[i].c_str()));
	}

	return ary;
}

/*
 * Class:     mapnik_Layer
 * Method:    setStyles
 * Signature: ([Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setStyles
  (JNIEnv *env, jobject layerobj, jobjectArray ary)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	std::vector<std::string> &styles(layer->styles());
	styles.clear();

	if (ary) {
		jsize size=env->GetArrayLength(ary);
		for (jsize i=0; i<size; i++) {
			jobject element=env->GetObjectArrayElement(ary, i);
			refjavastring s(env, (jstring)element);
			styles.push_back(s.stringz);
		}
	}
}

/*
 * Class:     mapnik_Layer
 * Method:    getMinZoom
 * Signature: ()D
 */
JNIEXPORT jdouble JNICALL Java_mapnik_Layer_getMinZoom
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return layer->getMinZoom();
}

/*
 * Class:     mapnik_Layer
 * Method:    setMinZoom
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setMinZoom
  (JNIEnv *env, jobject layerobj, jdouble z)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	layer->setMinZoom(z);
}

/*
 * Class:     mapnik_Layer
 * Method:    getMaxZoom
 * Signature: ()D
 */
JNIEXPORT jdouble JNICALL Java_mapnik_Layer_getMaxZoom
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return layer->getMaxZoom();
}

/*
 * Class:     mapnik_Layer
 * Method:    setMaxZoom
 * Signature: (D)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setMaxZoom
(JNIEnv *env, jobject layerobj, jdouble z)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	layer->setMaxZoom(z);
}

/*
 * Class:     mapnik_Layer
 * Method:    isActive
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_mapnik_Layer_isActive
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return (jboolean)layer->isActive();
}

/*
 * Class:     mapnik_Layer
 * Method:    setActive
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setActive
  (JNIEnv *env, jobject layerobj, jboolean b)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	layer->setActive((bool)b);
}


/*
 * Class:     mapnik_Layer
 * Method:    isQueryable
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_mapnik_Layer_isQueryable
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return (jboolean)layer->isQueryable();
}

/*
 * Class:     mapnik_Layer
 * Method:    setQueryable
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setQueryable
(JNIEnv *env, jobject layerobj, jboolean b)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	layer->setQueryable((bool)b);
}

/*
 * Class:     mapnik_Layer
 * Method:    isVisible
 * Signature: (D)Z
 */
JNIEXPORT jboolean JNICALL Java_mapnik_Layer_isVisible
  (JNIEnv *env, jobject layerobj, jdouble scale)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return layer->isVisible(scale);
}

/*
 * Class:     mapnik_Layer
 * Method:    isClearLabelCache
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_mapnik_Layer_isClearLabelCache
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return (jboolean)layer->clear_label_cache();
}

/*
 * Class:     mapnik_Layer
 * Method:    setClearLabelCache
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setClearLabelCache
(JNIEnv *env, jobject layerobj, jboolean b)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	layer->set_clear_label_cache((bool)b);
}

/*
 * Class:     mapnik_Layer
 * Method:    isCacheFeatures
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_mapnik_Layer_isCacheFeatures
(JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	return (jboolean)layer->cache_features();
}

/*
 * Class:     mapnik_Layer
 * Method:    setCacheFeatures
 * Signature: (Z)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setCacheFeatures
(JNIEnv *env, jobject layerobj, jboolean b)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	layer->set_cache_features((bool)b);
}

/*
 * Class:     mapnik_Layer
 * Method:    getDatasource
 * Signature: ()Lmapnik/Datasource;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Layer_getDatasource
  (JNIEnv *env, jobject layerobj)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);
	mapnik::datasource_ptr ds=layer->datasource();
	if (!ds) return 0;

	mapnik::datasource_ptr* dspinned=new mapnik::datasource_ptr(ds);
	jobject ret=env->AllocObject(CLASS_DATASOURCE.java_class);
	env->SetLongField(ret, CLASS_DATASOURCE.ptr_field, FROM_POINTER(dspinned));

	return ret;
}

/*
 * Class:     mapnik_Layer
 * Method:    setDatasource
 * Signature: (Lmapnik/Datasource;)V
 */
JNIEXPORT void JNICALL Java_mapnik_Layer_setDatasource
  (JNIEnv *env, jobject layerobj, jobject dsobject)
{
	mapnik::layer* layer=LOAD_LAYER_POINTER(layerobj);

	if (!dsobject) {
		layer->set_datasource(mapnik::datasource_ptr());
		return;
	}

	mapnik::datasource_ptr* dspinned=
			static_cast<mapnik::datasource_ptr*>(
					TO_POINTER(env->GetLongField(dsobject, CLASS_DATASOURCE.ptr_field)));
	layer->set_datasource(*dspinned);
}

// -- Datasource
/*
 * Class:     mapnik_Datasource
 * Method:    dealloc
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mapnik_Datasource_dealloc
  (JNIEnv *env, jclass c, jlong ptr)
{
	mapnik::datasource_ptr* dspinned=
			static_cast<mapnik::datasource_ptr*>(
					TO_POINTER(ptr));
	delete dspinned;

}


/*
 * Class:     mapnik_Datasource
 * Method:    getParameters
 * Signature: ()Lmapnik/Parameters;
 */
JNIEXPORT jobject JNICALL Java_mapnik_Datasource_getParameters
  (JNIEnv *env, jobject dsobj)
{
	mapnik::datasource_ptr* dsp=LOAD_DATASOURCE_POINTER(dsobj);
	const mapnik::parameters& params((*dsp)->params());

	jobject paramobject=env->NewObject(CLASS_PARAMETERS, CTOR_PARAMETERS);

	for (mapnik::param_map::const_iterator iter=params.begin(); iter!=params.end(); iter++) {
		jstring key=env->NewStringUTF(iter->first.c_str());
		boost::apply_visitor(translate_parameter_visitor(env, paramobject, key), iter->second);
	}

	return paramobject;
}


//// -- DatasourceCache
/*
 * Class:     mapnik_DatasourceCache
 * Method:    registerDatasources
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_mapnik_DatasourceCache_registerDatasources
  (JNIEnv *env, jclass c, jstring sj)
{
	try {
		refjavastring path(env, sj);
		mapnik::datasource_cache::register_datasources(path.stringz);
	} catch (std::exception& e) {
		throw_java_exception(env, e);
		return;
	}
}

/*
 * Class:     mapnik_DatasourceCache
 * Method:    create
 * Signature: (Lmapnik/Parameters;Z)Lmapnik/Datasource;
 */
JNIEXPORT jobject JNICALL Java_mapnik_DatasourceCache_create
	(JNIEnv *env, jclass c, jobject paramsmap, jboolean bind)
{
	try {
		mapnik::parameters params;
		translate_to_mapnik_parameters(env, paramsmap, params);

		mapnik::datasource_ptr ds=mapnik::datasource_cache::create(params, (bool)bind);
		if (!ds) return 0;

		mapnik::datasource_ptr *dspinned=new mapnik::datasource_ptr(ds);
		jobject ret=env->AllocObject(CLASS_DATASOURCE.java_class);
		env->SetLongField(ret, CLASS_DATASOURCE.ptr_field, FROM_POINTER(dspinned));
		return ret;
	} catch (std::exception& e) {
		throw_java_exception(env, e);
		return 0;
	}
}

/// -- FeatureTypeStyle class
/*
 * Class:     mapnik_FeatureTypeStyle
 * Method:    alloc
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_mapnik_FeatureTypeStyle_alloc
  (JNIEnv *env, jclass c)
{
	return FROM_POINTER(new mapnik::feature_type_style());
}

/*
 * Class:     mapnik_FeatureTypeStyle
 * Method:    dealloc
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_mapnik_FeatureTypeStyle_dealloc
  (JNIEnv *env, jclass c, jlong ptr)
{
	mapnik::feature_type_style* style=static_cast<mapnik::feature_type_style*>(TO_POINTER(ptr));
	delete style;
}

/*
 * Class:     mapnik_FeatureTypeStyle
 * Method:    collectAttributes
 * Signature: ()Ljava/util/Set;
 */
JNIEXPORT jobject JNICALL Java_mapnik_FeatureTypeStyle_collectAttributes
  (JNIEnv *env, jobject styleobject)
{
	mapnik::feature_type_style* style=LOAD_FEATURE_TYPE_STYLE_POINTER(styleobject);
	const std::vector<mapnik::rule>& rules(style->get_rules());

	std::set<std::string> attrs;
	mapnik::attribute_collector collector(attrs);
	BOOST_FOREACH(mapnik::rule const& r, rules) {
		collector(r);
	}


	jobject ret=env->NewObject(CLASS_HASHSET, CTOR_HASHSET);
	for (std::set<std::string>::iterator iter=attrs.begin(); iter!=attrs.end(); iter++) {
		env->CallBooleanMethod(ret, METHOD_HASHSET_ADD, env->NewStringUTF(iter->c_str()));
	}

	return ret;
}

/// -- Mapnik class
/*
 * Class:     mapnik_Mapnik
 * Method:    nativeInit
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_mapnik_Mapnik_nativeInit
  (JNIEnv *env, jclass c)
{
	if (initialized) return;
	if (init_ids(env))
		initialized=true;
}
