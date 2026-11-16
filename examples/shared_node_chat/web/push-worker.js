const cacheName='shared-chat-push-v1:'+self.location.pathname;
self.addEventListener('message', event => {
  if(event.data?.type!=='chat-url')return;
  const url=new URL(event.data.url);
  if(url.origin!==self.location.origin)return;
  event.waitUntil(caches.open(cacheName).then(cache=>
    cache.put('return-url',new Response(url.href))));
});

self.addEventListener('push', event => {
  event.waitUntil((async()=>{
    const windows=await clients.matchAll({type:'window',includeUncontrolled:true});
    if(windows.some(window=>window.visibilityState==='visible'))return;
    const cache=await caches.open(cacheName);
    const response=await cache.match('return-url');
    const url=response?await response.text():new URL('./',self.location.href).href;
    await self.registration.showNotification('SharedNode Chat',{
      body:'New message',tag:'shared-chat-message',data:{url}
    });
  })());
});

self.addEventListener('notificationclick',event=>{
  event.notification.close();
  event.waitUntil((async()=>{
    const url=event.notification.data?.url||new URL('./',self.location.href).href;
    const windows=await clients.matchAll({type:'window',includeUncontrolled:true});
    const open=windows.find(window=>window.url===url);
    if(open){await open.focus();return;}
    await clients.openWindow(url);
  })());
});
