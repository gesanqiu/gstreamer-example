# Threads

GStreamer的设计原生支持多线程，并完全保证线程安全。大多数情况下，多线程实现细节对基于GStreamer开发的应用程序隐藏，因为这会让应用程序开发更便利。而在某些场景下，应用程序可能会介入Gstreamer的多线程机制。此时，Gstreamer允许应用程序指定在流水线内的某些部分使用多线程。具体请参考《When would you want to force a thread?》一文。

Gstreamer还支持开发人员在线程被创建时获取通知。从而，开发人员可以配置线程的优先级，设置线程池的相关行为等。具体请参考《Configuring Threads in GStreamer》一文。

## Scheduling in GStreamer

GStreamer中的每一个element可以决定自己的数据调度方式，即element内pad的数据调度是使用push模式还是pull模式。举例来说，一个element可以选择开启一个线程从自己的sink-pad中拉取数据，或者开启一个线程向自己的source-pad中推送数据。并且，element还支持数据处理过程中的上下游线程各自设置为push或pull模式。换句话说，GStreamer不会对element选择哪种数据调度方式做出限制。更多具体信息请参考插件编写指南。

不管哪种调度方式，流水线中一定会存在某些element开启线程处理数据，我们称这些线程为“streaming threads”。在代码中，“streaming threads”往往是一个GstTask实例，它们统一从一个GstTaskPool中被创建。在下一节，我们会学习如果从GstTask，GstTaskPool获取消息并进行配置。

## Configuring Threads in GStreamer

一条 `STREAM_STATUS` 消息

- 当

- 当

- 你

我们

### Boost priority of a thread

让

- 当

- 此

- 或者

在

这个

在

注意

当

## When would you want to force a thread?

我们

这里有

- 数据

- 同步

以上
为了
