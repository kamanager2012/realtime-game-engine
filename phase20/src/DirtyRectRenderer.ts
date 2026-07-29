/**
 * 脏矩形渲染器
 * 只重绘发生变化的区域，避免每帧全量 Canvas 绘制
 */
class DirtyRectRenderer {
  private canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private width: number;
  private height: number;
  
  // 脏矩形列表
  private dirtyRects: DOMRect[] = [];
  
  // 层级缓存（静态背景不重绘）
  private backgroundCache: ImageData | null = null;
  private staticCache: OffscreenCanvas;
  private dynamicCache: OffscreenCanvas;
  
  // 需要完全重绘的区域（如动画效果）
  private fullRedrawZones: Set<string> = new Set();
  
  constructor(canvas: HTMLCanvasElement, width: number, height: number) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d')!;
    this.width = width;
    this.height = height;
    
    this.staticCache = new OffscreenCanvas(width, height);
    this.dynamicCache = new OffscreenCanvas(width, height);
    
    canvas.width = width;
    canvas.height = height;
  }
  
  /**
   * 标记脏区域
   * 下次渲染循环只重绘这些区域
   */
  markDirty(rect: DOMRect): void {
    this.dirtyRects.push(rect);
  }
  
  /**
   * 标记元素区域为脏
   * 基于元素边界框
   */
  markElementDirty(x: number, y: number, w: number, h: number): void {
    // 添加一些边距防止裁切
    const margin = 2;
    this.dirtyRects.push(new DOMRect(
      x - margin, y - margin,
      w + margin * 2, h + margin * 2
    ));
  }
  
  /**
   * 标记需要全量重绘的区域（如底池动画）
   */
  markFullRedrawZone(zoneId: string, rect: DOMRect): void {
    this.fullRedrawZones.add(zoneId);
    this.dirtyRects.push(rect);
  }
  
  /**
   * 清除全量重绘标记
   */
  clearZone(zoneId: string): void {
    this.fullRedrawZones.delete(zoneId);
  }
  
  /**
   * 缓存静态背景（牌桌、边框等不变化的元素）
   * 只在初始化或主题变化时调用
   */
  cacheBackground(drawFn: (ctx: OffscreenCanvasRenderingContext2D) => void): void {
    const bgCtx = this.staticCache.getContext('2d')!;
    bgCtx.clearRect(0, 0, this.width, this.height);
    drawFn(bgCtx);
    this.backgroundCache = bgCtx.getImageData(0, 0, this.width, this.height);
  }
  
  /**
   * 执行渲染
   * 合并重叠的脏矩形，只重绘必要区域
   */
  render(drawDynamicFn: (
    ctx: CanvasRenderingContext2D, 
    rect: DOMRect
  ) => void): void {
    if (this.dirtyRects.length === 0) return;
    
    // 1. 合并重叠/相邻的脏矩形
    const merged = this.mergeRects(this.dirtyRects);
    this.dirtyRects = [];
    
    // 2. 遍历每个合并后的脏区域
    for (const rect of merged) {
      // 检查是否需要全量重绘
      const needsFullRedraw = this.checkFullRedraw(rect);
      
      if (needsFullRedraw) {
        // 全量重绘该区域
        this.ctx.clearRect(rect.x, rect.y, rect.width, rect.height);
        
        // 先从背景缓存恢复
        if (this.backgroundCache) {
          this.ctx.putImageData(
            this.backgroundCache,
            rect.x, rect.y,
            rect.x, rect.y,
            rect.width, rect.height
          );
        }
        
        // 绘制动态内容
        drawDynamicFn(this.ctx, rect);
      } else {
        // 增量更新 - 只重绘变化部分
        this.ctx.save();
        this.ctx.beginPath();
        this.ctx.rect(rect.x, rect.y, rect.width, rect.height);
        this.ctx.clip();
        
        // 恢复背景
        if (this.backgroundCache) {
          this.ctx.putImageData(
            this.backgroundCache,
            rect.x, rect.y,
            rect.x, rect.y,
            rect.width, rect.height
          );
        }
        
        // 绘制该区域的动态元素
        drawDynamicFn(this.ctx, rect);
        this.ctx.restore();
      }
    }
  }
  
  /**
   * 强制全量重绘（必要时调用，如窗口 resize）
   */
  forceFullRedraw(): void {
    this.dirtyRects.push(new DOMRect(0, 0, this.width, this.height));
  }
  
  /**
   * 合并重叠的矩形
   * 使用简单的扫描线算法
   */
  private mergeRects(rects: DOMRect[]): DOMRect[] {
    if (rects.length <= 1) return rects;
    
    // 简化的贪婪合并
    const result: DOMRect[] = [];
    const sorted = [...rects].sort((a, b) => a.x - b.x || a.y - b.y);
    
    let current = sorted[0];
    
    for (let i = 1; i < sorted.length; i++) {
      const next = sorted[i];
      
      // 检查是否重叠或相邻（阈值 8px）
      const overlapThreshold = 8;
      
      if (this.isOverlapping(current, next, overlapThreshold)) {
        // 合并
        const x1 = Math.min(current.x, next.x);
        const y1 = Math.min(current.y, next.y);
        const x2 = Math.max(current.x + current.width, next.x + next.width);
        const y2 = Math.max(current.y + current.height, next.y + next.height);
        
        current = new DOMRect(x1, y1, x2 - x1, y2 - y1);
      } else {
        result.push(current);
        current = next;
      }
    }
    
    result.push(current);
    return result;
  }
  
  /**
   * 检查两个矩形是否重叠或相邻
   */
  private isOverlapping(a: DOMRect, b: DOMRect, threshold: number): boolean {
    return !(
      a.x + a.width + threshold < b.x ||
      b.x + b.width + threshold < a.x ||
      a.y + a.height + threshold < b.y ||
      b.y + b.height + threshold < a.y
    );
  }
  
  /**
   * 检查脏矩形是否需要全量重绘
   */
  private checkFullRedraw(rect: DOMRect): boolean {
    // 如果脏矩形覆盖大部分画布，直接全量
    const area = rect.width * rect.height;
    const totalArea = this.width * this.height;
    if (area > totalArea * 0.5) return true;
    
    // 检查区域内是否有全量重绘标记的 zone
    for (const zone of this.fullRedrawZones) {
      // 简化：如果区域内有动画，直接全量
      if (zone.startsWith('anim_')) return true;
    }
    
    return false;
  }
  
  getDirtyCount(): number {
    return this.dirtyRects.length;
  }
}

/**
 * 动画脏矩形追踪器
 * 用于追踪动画产生的脏区域
 */
class AnimationTracker {
  private activeAnimations: Map<string, AnimationFrame> = new Map();
  private dirtyRenderer: DirtyRectRenderer;
  
  constructor(renderer: DirtyRectRenderer) {
    this.dirtyRenderer = renderer;
  }
  
  /**
   * 注册一个动画效果（如筹码移动、翻牌）
   */
  registerAnimation(
    id: string,
    startRect: DOMRect,
    endRect: DOMRect,
    durationMs: number
  ): void {
    this.activeAnimations.set(id, {
      startRect,
      endRect,
      startTime: performance.now(),
      duration: durationMs,
    });
    
    // 标记起始区域为脏
    this.dirtyRenderer.markDirty(startRect);
  }
  
  /**
   * 每帧调用，更新动画脏区域
   */
  tick(): void {
    const now = performance.now();
    
    for (const [id, anim] of this.activeAnimations) {
      const elapsed = now - anim.startTime;
      const progress = Math.min(elapsed / anim.duration, 1);
      
      // 计算当前帧的插值矩形
      const currentX = anim.startRect.x + 
        (anim.endRect.x - anim.startRect.x) * progress;
      const currentY = anim.startRect.y + 
        (anim.endRect.y - anim.startRect.y) * progress;
      
      const currentRect = new DOMRect(
        currentX, currentY,
        anim.endRect.width, anim.endRect.height
      );
      
      // 标记当前位置为脏
      this.dirtyRenderer.markDirty(currentRect);
      
      if (progress >= 1.0) {
        this.activeAnimations.delete(id);
      }
    }
  }
  
  hasActiveAnimations(): boolean {
    return this.activeAnimations.size > 0;
  }
}

interface AnimationFrame {
  startRect: DOMRect;
  endRect: DOMRect;
  startTime: number;
  duration: number;
}

export { DirtyRectRenderer, AnimationTracker };
