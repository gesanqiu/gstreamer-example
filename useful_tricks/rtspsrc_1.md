# GStreamer源码剖析之——rtspsrc（1）

>  RTSP URL密码中包含'@'的解决方法。

**前言：**为了让Application的Stream Pipeline具有更好的适应性，能够处理不同格式的输入流，在设计Pipeline时使用了`uridecodebin`作为source plugin，但随着客户越来越多，各种格式的url就暴露出来，最初发现`uridecodebin`的`uri`属性无法处理密码中含有`@`字符的rtsp-url，但是VLC能够正常拉流播放。从本质上来说这是个url解析的问题，我针对这个问题设计了一系列rtsp-url，包含各种特殊字符，这就导致parse函数的实现变得非常困难，但转念一想我们并没有必要处理所有的情况，既然以GStreamer为基础框架，那么针对RTSP流，显然都是使用`rtspsrc`插件来处理的，那么只需要参考`rtspsrc`的实现即可，于是这就成了我`rtspsrc`源码剖析的第一篇。

注1：GStreamer源码托管在GitLab上，Github上的源码只是GitLab的镜像，更新以及分支并不全，因此推荐到GitLab进行阅读源码（当然也可以将源码clone到本地进行阅读）。

注2：由于开发平台是Ubuntu 18.04，Ubuntu18.04默认的GStreamer版本为1.14.5，因此以下解析也选用这一版本，但就这一篇解析内容而言，最新版1.20GStreamer源码并无改动。

### rtspsrc.c

`uridecodebin`在处理RTSP流时，在处理`source-setup`信号时会创建`rtspsrc`插件来处理RTSP流，相关的uri也被传递给`rtspsrc`。通过`gst-inspect-1.0`工具或者是从[rtspsrc properties reference](https://gstreamer.freedesktop.org/documentation/rtsp/rtspsrc.html?gi-language=c#properties)可以猜测我们需要关注的属性是`location`，因为`location`是`rtspsrc`的url属性，于是我们可以在`rtspsrc.c`中着重关注`location`属性值影响的过程。

#### gst_rtspsrc_uri_set_uri

在`rtspstr.c`中搜索`PROC_LOCATION`可知location属性的setter会调用`gst_rtspsrc_uri_set_uri`。在不考虑rtsp-sdp的情况下，核心代码如下：

```c
  } else {
    /* try to parse */
    GST_DEBUG_OBJECT (src, "parsing URI");
    if ((res = gst_rtsp_url_parse (uri, &newurl)) < 0)
      goto parse_error;
  }

  /* if worked, free previous and store new url object along with the original
   * location. */
  GST_DEBUG_OBJECT (src, "configuring URI");
  g_free (src->conninfo.location);
  src->conninfo.location = g_strdup (uri);
  gst_rtsp_url_free (src->conninfo.url);
  src->conninfo.url = newurl;
  g_free (src->conninfo.url_str);
  if (newurl)
    src->conninfo.url_str = gst_rtsp_url_get_request_uri (src->conninfo.url);
  else
    src->conninfo.url_str = NULL;
```

- uri(location)被直接赋值给`src->conninfo.location`；
- 调用`gst_rtsp_url_parse`将uri(location)解析至`GstRTSPUrl`结构体变量newurl中；
- 调用`gst_rtsp_url_get_request_uri`将过滤掉user-id和user-pw子段的url赋值给`src->conninfo.url_str`（代码比较简单所以不做展开）；

注：仅分析IPV4流地址。

#### gst_rtsp_url_parse (const gchar * urlstr, GstRTSPUrl ** url)

首先看`gst_rtsp_url_parse`的两个参数，一个是urlstr字符串，另一个是`GstRTSPUrl`结构体指针的指针，也即会将urlstr的解析结果存储在传入url变量中并返回。

那么首先来看`GstRTSPUrl`结构体的声明：

```c
struct _GstRTSPUrl {
  GstRTSPLowerTrans  transports;
  GstRTSPFamily      family;
  gchar             *user;
  gchar             *passwd;
  gchar             *host;
  guint16            port;
  gchar             *abspath;
  gchar             *query;
};
```

可以看到它含有一个rtsp-url的所有的组成片段，包含rtsp验证用的用户名和用户密码，主机地址，端口地址，主/子码流绝对路径，对rtsp服务器的请求。

注：`gst_rtsp_url_parse`源码过长，具体可以浏览[rtspurl.c](https://github.com/GStreamer/gst-plugins-base/blob/master/gst-libs/gst/rtsp/gstrtspurl.c)，以下仅分析源码的解析过程。

- malloc一个GstRTSPUrl指针`res`，用于赋给*url；
- 首先查找`://`用以确认rtsp的协议以及流地址的起点；
- 查找`://`之后的第一个`/`或`?`，标记为`delim`；
- 查找`://`和`/`（或`?`）之间的第一个`:`和`@`，分割出`res->user`和`res->passwd`（会对这两个字符串出做ascii转义处理）；
- 查找`@`之后的第一个`:`，分割出`res->host`和`res->port`；
- 根据`delim`是`/`还是`?`，先分割出`res->abspath`，然后再分割出`res->query`。

可以看出`gst_rtsp_url_parse`的解析是比较粗糙的，它首先找到urlstr中`://`后的第一个`/`，并以此为界限划分url，然后就直接通过**第一个**`:`和`@`来分割用户部分和host部分，并且在处理时只用了`strchr`做字符匹配，也即直接使用location是无法处理用户密码中包含`@`的情况的。

这说明我们无法直接通过将uridecodebin的uri属性传给rtspsrc的location属性来解决这个问题，但是从上面的代码可以看到`src->conninfo.url_str`是一条不含user和passwd的url。并且阅读文档可以看到rtspsrc提供了两个额外的属性`user-id`和`user-pw`用户单独存储rtsp的验证信息，于是问题转化成了如何从传递给uridecodebin的uri中分割出user和passwd。

#### 测试

- `user-id`和`user-pw`的使用，测试通过。
- 转义URL：根据[elasticRTC - cannot play streams with @ in rtsp password](https://github.com/Kurento/bugtracker/issues/173)这个issue提供的解决方案，需要用户将账号密码中的特殊字符全部提前转义成ASCII码即可，例如将`@`转义为`%40`，测试通过。

#### 约束

注1：以下约束以海康为测试摄像头。

注2：以官方给出的url格式`rtsp[u]://[user:passwd@]host[:port]/abspath[?query]`为标准。

- `passwd`字段中不能含有`/`：

  从`rtspurl.c`的实现来看，它也并非是完美的，通过上文的源码剖析可以知道，`gst_rtsp_url_parse()`是以`/`为分割点，区分IP地址和码流绝对路径两部分内容，也即在这个`/`之前的IP地址即用户信息中是不能包含`/`的。虽然摄像头的密码支持添加特殊字符，但实际我们在密码中加入`/`后，无论是GStreamer的rtspsrc和VLC都无法正确解析URL通过RTSP流的用户认证。

- `user-id`字段中不能含有特殊字符：

  `gst_rtsp_url_parse()`对`user`和`passwd`的唯一特殊处理在于调用了`g_uri_unescape_segment()`使得其能够支持例如**使用ASCII码等编码格式转义后的用户名和密码**，但是这需要用户提前将`user`和`passwd`字段进行转义，这对于研发人员来说并不困难，但对于普通用户来说会显得比较麻烦。但是进一步的我们在摄像头管理页面尝试修改`user-id`字段使其包含特殊字符时，页面会提示不支持，所以我直接沿用这一约束。

**有了如上约束之后，我们可以以****`://`**之后的第一个**`/`**为基准，找这个**`/`**之前的第一个**`@`**（或者是最后一个）作为passwd和host的分割点，进而再找第一个**`:`**将user-id和user-pw分割出来，然后在uridecodebin的source-setup回调中传给rtspsrc即可。**