const {readFileSync}=require('node:fs');
const {join}=require('node:path');
const {runInNewContext}=require('node:vm');
const assert=require('node:assert/strict');
const test=require('node:test');

test('background push shows a notification; visible chat suppresses it; click restores chat',async()=>{
  const handlers=new Map(), notifications=[], opened=[];
  let windows=[], stored;
  const worker={
    location:new URL('https://chat.example/push-worker.js'),
    registration:{showNotification:async(...args)=>notifications.push(args)},
    addEventListener:(name,handler)=>handlers.set(name,handler)
  };
  const context={
    self:worker,URL,Response,
    caches:{open:async()=>({
      put:async(_,response)=>{stored=await response.text()},
      match:async()=>stored?new Response(stored):undefined
    })},
    clients:{matchAll:async()=>windows,openWindow:async url=>opened.push(url)}
  };
  runInNewContext(readFileSync(join(__dirname,'push-worker.js'),'utf8'),context);
  const url='https://chat.example/index.html?profile=alice';
  let done;
  handlers.get('message')({data:{type:'chat-url',url},waitUntil:p=>done=p});
  await done;
  windows=[{visibilityState:'visible',url}];
  handlers.get('push')({waitUntil:p=>done=p});
  await done;
  assert.equal(notifications.length,0);
  windows=[];
  handlers.get('push')({waitUntil:p=>done=p});
  await done;
  assert.equal(notifications.length,1);
  assert.equal(notifications[0][1].data.url,url);
  let closed=false;
  handlers.get('notificationclick')({
    notification:{data:{url},close:()=>{closed=true}},waitUntil:p=>done=p
  });
  await done;
  assert.equal(closed,true);
  assert.deepEqual(opened,[url]);
});
