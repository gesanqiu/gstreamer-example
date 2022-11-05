# Threads

GStreamer的设计原生支持多线程，并完全保证线程安全。大多数情况下，多线程实现细节对基于GStreamer开发的应用程序隐藏，因为这会让应用程序开发更便利。而在某些场景下，应用程序可能会介入Gstreamer的多线程机制。此时，Gstreamer允许应用程序指定在流水线内的某些部分使用多线程。具体请参考《When would you want to force a thread?》一文。

Gstreamer还支持开发人员在线程被创建时获取通知。从而，开发人员可以配置线程的优先级，设置线程池的相关行为等。具体请参考《Configuring Threads in GStreamer》一文。

## Scheduling in GStreamer

GStreamer中的每一个element可以决定自己的数据调度方式，即element内pad的数据调度是使用push模式还是pull模式。举例来说，一个element可以选择开启一个线程从自己的sink-pad中拉取数据，或者开启一个线程向自己的source-pad中推送数据。并且，element还支持数据处理过程中的上下游线程各自设置为push或pull模式。换句话说，GStreamer不会对element选择哪种数据调度方式做出限制。更多具体信息请参考插件编写指南。

不管哪种调度方式，流水线中一定会存在某些element开启线程处理数据，我们称这些线程为“streaming threads”。在代码中，“streaming threads”往往是一个GstTask实例，它们统一从一个GstTaskPool中被创建。在下一节，我们会学习如果从GstTask，GstTaskPool获取消息并进行配置。

## Configuring Threads in GStreamer

“streaming threads”的运行状态会经由发布在GstBus中的STREAM_STATUS消息通知应用程序。根据运行状态不同，消息被分为以下多种类型：

- 当一个新的线程将要被创建时，会有一条类型为GST_STREAM_STATUS_TYPE_CREATE的STREAM_STATUS消息被发出。只有在接收到这条消息后，应用程序才能为GstTask配置GstTaskPool。此时配置的GstTaskPool可由应用程序定制，定制后的任务池会提供线程来实现“streaming threads”的具体需求。
  
  需要注意的是，当应用程序在定制化GstTaskPool时，必须以同步方式处理STREAM_STATUS消息；当应用程序不定制GstTaskPool时，处理STREAM_STATUS消息的函数返回后，GstTask会使用默认的GstTaskPool。

- 当一个线程进入或离开时，应用程序可以配置这个线程的优先级。当线程被销毁时，应用程序也会得到STREAM_STATUS消息通知。

- 当线程开启、暂停和终止时，应用程序也会得到STREAM_STATUS消息通知。在GUI应用中。这些消息可以被用来可视化“streaming threads”的运行状态。

下一小节中会介绍一个具体的示例。

### Boost priority of a thread

```
.----------.    .----------.
| fakesrc  |    | fakesink |
|         src->sink        |
'----------'    '----------'
```

以上图的简单流水线为例。其中的数据调度模式为，fakesrc开启“streaming threads”以生成虚拟数据，同时它还使用push模式将数据推送给fakesink。应用程序若想提升“streaming threads”的优先级则可通过下述方法实现：

- 当流水线状态从READY变为PAUSED时，fakesrc生成一条STREAM_STATUS消息，表示其需要一个“streaming threads”来将数据推送给fakesink。

- 应用程序从bus上收到这条消息时，会使用同步方式调用一个bus响应函数处理此消息。在该函数中，应用程序会为消息中传递而来的GstTask实例配置一个定制化的GstTaskPool。这个定制化的任务池会负责创建线程。而我们提升“streaming threads”优先级的需求可以在任务池创建线程时完成。

- 另一种提高“streaming threads”优先级的方法如下。利用这条消息是被bus响应函数同步方式处理的特性，此时响应函数已经在一个线程内，应用程序可以使用ENTER/LEAVE通知来提升当前线程的优先级，甚至是操作系统对当前线程的调度策略。

在上段第一点中，我们需要实现一个配置给GstTask实例的定制化GstTaskPool。下面的代码就是一个定制化GstTaskPool的实现。实现方式使用Gobject的派生方式，从GstTaskPool派生出子类

这个TestRTPool。这个子类的push方法中，应用程序使用pthread创建了一个SCHED_RR轮转法实时线程。注意，创建实时线程可能要求应用程序获得更多的系统权限。

```c
#include <pthread.h>

typedef struct
{
  pthread_t thread;
} TestRTId;

G_DEFINE_TYPE (TestRTPool, test_rt_pool, GST_TYPE_TASK_POOL);

static void
default_prepare (GstTaskPool * pool, GError ** error)
{
  /* we don't do anything here. We could construct a pool of threads here that
   * we could reuse later but we don't */
}

static void
default_cleanup (GstTaskPool * pool)
{
}

static gpointer
default_push (GstTaskPool * pool, GstTaskPoolFunction func, gpointer data,
    GError ** error)
{
  TestRTId *tid;
  gint res;
  pthread_attr_t attr;
  struct sched_param param;

  tid = g_slice_new0 (TestRTId);

  pthread_attr_init (&attr);
  if ((res = pthread_attr_setschedpolicy (&attr, SCHED_RR)) != 0)
    g_warning ("setschedpolicy: failure: %p", g_strerror (res));

  param.sched_priority = 50;
  if ((res = pthread_attr_setschedparam (&attr, &param)) != 0)
    g_warning ("setschedparam: failure: %p", g_strerror (res));

  if ((res = pthread_attr_setinheritsched (&attr, PTHREAD_EXPLICIT_SCHED)) != 0)
    g_warning ("setinheritsched: failure: %p", g_strerror (res));

  res = pthread_create (&tid->thread, &attr, (void *(*)(void *)) func, data);

  if (res != 0) {
    g_set_error (error, G_THREAD_ERROR, G_THREAD_ERROR_AGAIN,
        "Error creating thread: %s", g_strerror (res));
    g_slice_free (TestRTId, tid);
    tid = NULL;
  }

  return tid;
}

static void
default_join (GstTaskPool * pool, gpointer id)
{
  TestRTId *tid = (TestRTId *) id;

  pthread_join (tid->thread, NULL);

  g_slice_free (TestRTId, tid);
}

static void
test_rt_pool_class_init (TestRTPoolClass * klass)
{
  GstTaskPoolClass *gsttaskpool_class;

  gsttaskpool_class = (GstTaskPoolClass *) klass;

  gsttaskpool_class->prepare = default_prepare;
  gsttaskpool_class->cleanup = default_cleanup;
  gsttaskpool_class->push = default_push;
  gsttaskpool_class->join = default_join;
}

static void
test_rt_pool_init (TestRTPool * pool)
{
}

GstTaskPool *
test_rt_pool_new (void)
{
  GstTaskPool *pool;

  pool = g_object_new (TEST_TYPE_RT_POOL, NULL);

  return pool;
}
```

上述最关键的是default_push函数。它需要启动一个新线程运行从形参func传入的指定函数。现实中更适当做法可能是在线程池中多准备一些线程，避免频繁的线程创建和销毁所带来的不必要开销。

在下一段代码中，应用程序开始真正为fakesrc配置上述生成的定制化GstTaskPool。如前文所述，我们需要一个同步的bus响应函数来处理STREAM_STATUS消息，从而在获得GST_STREAM_STATUS_TYPE_CREATE类型消息时，为GstTaskl配置定制化GstTaskPool。

```c
static GMainLoop* loop;

static void
on_stream_status (GstBus     *bus,
                  GstMessage *message,
                  gpointer    user_data)
{
  GstStreamStatusType type;
  GstElement *owner;
  const GValue *val;
  GstTask *task = NULL;

  gst_message_parse_stream_status (message, &type, &owner);

  val = gst_message_get_stream_status_object (message);

  /* see if we know how to deal with this object */
  if (G_VALUE_TYPE (val) == GST_TYPE_TASK) {
    task = g_value_get_object (val);
  }

  switch (type) {
    case GST_STREAM_STATUS_TYPE_CREATE:
      if (task) {
        GstTaskPool *pool;

        pool = test_rt_pool_new();

        gst_task_set_pool (task, pool);
      }
      break;
    default:
      break;
  }
}

static void
on_error (GstBus     *bus,
          GstMessage *message,
          gpointer    user_data)
{
  g_message ("received ERROR");
  g_main_loop_quit (loop);
}

static void
on_eos (GstBus     *bus,
        GstMessage *message,
        gpointer    user_data)
{
  g_main_loop_quit (loop);
}

int
main (int argc, char *argv[])
{
  GstElement *bin, *fakesrc, *fakesink;
  GstBus *bus;
  GstStateChangeReturn ret;

  gst_init (&argc, &argv);

  /* create a new bin to hold the elements */
  bin = gst_pipeline_new ("pipeline");
  g_assert (bin);

  /* create a source */
  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  g_assert (fakesrc);
  g_object_set (fakesrc, "num-buffers", 50, NULL);

  /* and a sink */
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (fakesink);

  /* add objects to the main pipeline */
  gst_bin_add_many (GST_BIN (bin), fakesrc, fakesink, NULL);

  /* link the elements */
  gst_element_link (fakesrc, fakesink);

  loop = g_main_loop_new (NULL, FALSE);

  /* get the bus, we need to install a sync handler */
  bus = gst_pipeline_get_bus (GST_PIPELINE (bin));
  gst_bus_enable_sync_message_emission (bus);
  gst_bus_add_signal_watch (bus);

  g_signal_connect (bus, "sync-message::stream-status",
      (GCallback) on_stream_status, NULL);
  g_signal_connect (bus, "message::error",
      (GCallback) on_error, NULL);
  g_signal_connect (bus, "message::eos",
      (GCallback) on_eos, NULL);

  /* start playing */
  ret = gst_element_set_state (bin, GST_STATE_PLAYING);
  if (ret != GST_STATE_CHANGE_SUCCESS) {
    g_message ("failed to change state");
    return -1;
  }

  /* Run event loop listening for bus messages until EOS or ERROR */
  g_main_loop_run (loop);

  /* stop the bin */
  gst_element_set_state (bin, GST_STATE_NULL);
  gst_object_unref (bus);
  g_main_loop_unref (loop);

  return 0;
}
```

注意，上述代码很可能需要应用程序获得root权限。当不能创建线程时，gst_element_set_state将会运行失败，失败的返回值会被应用程序所捕获。

当流水线中存在多个线程时，应用程序会同时收到多条STREAM_STATUS消息。可以通过消息所有者（所有者往往是启动这个消息所对应线程的pad或element）来区分这些消息，从而明确每个消息所对应线程运行的是整个应用程序上下文中的哪个函数。

## When would you want to force a thread?

我们在上文中已经看到element自身如何创建线程。除此以外，还可以通过向流水线中添加特定类型element的方式强制流水线使用独立线程完成部分任务。

从计算性能角度考虑，应用程序不应该为流水线中每一个element都强制使用独立线程，因为这会导致不必要的计算开销。反之，推荐在流水线中强制使用独立线程的场景有：

- 数据缓存。当应用程序在处理互联网流式数据时或者从声卡、显卡里记录实时数据流时，数据缓存就十分必要。并且数据缓存可以降低流水线中某个环节突然卡顿导致数据丢失而产生的影响。具体示例可以参见《Stream buffering》一文，那里举例了queue2组件在缓存互联网数据时的作用。
  
  ![](D:\code\gstreamer-example\basic_theory\app_dev_manual\images\thread-buffering.png)

- 同步输出设备。当流水线中的数据同时包含视频和音频时，并且两者都作为流水线的独立输出。通过为它们各自添加独立线程，就能做到音视频相互独立解析，并且提供更好的音视频同步方式。
  
  ![](D:\code\gstreamer-example\basic_theory\app_dev_manual\images\thread-synchronizing.png)


阅读至此，您可能已经发现在流水线中强制使用独立线程的特殊类型element就是“queue”。queue作为线程隔离组件提供流水线中强制使用新线程的能力。它的实现是基于众所周知的生产者/消费者模式，除了能提供跨线程安全的数据流通功能，它本身还可以用作缓存空间。queue包含若干个基于GObject实现的属性，可以调节这些属性实现特定业务。例如，可以实现数据流量的上下界控制。queue的下界属性默认情况下不启用，但当应用程序指定了数据流量下界时，若没有足够的数据量通过queue传递，则queue不会输出任何数据。上界属性则代表，如果传输中的数据量超过上界阈值，则queue会根据其他属性的配置，或阻挡更多数据的流入，或开启不同的数据丢弃功能。

在流水线中使用queue的方式也很简单，仅需在构建流水线时为必要的位置添加queue即可。其他关于线程的细节会由GStreamer在内部完成。
